package com.yeziyi.garageble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothGattService
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.content.Context
import android.os.Build
import java.util.UUID

/**
 * BLE（低功耗蓝牙）串口管理 —— 对应 HM-10 / AT-09 / JDY-xx 这类"BLE 串口透传"模块。
 *
 * 和 [BluetoothSppManager] 的区别：
 *   HC-05 / HC-06 是经典蓝牙，走 RFCOMM(SPP)，用 BluetoothSocket；
 *   BLE 串口模块走 GATT，要在"服务 → 特征值"上收/发数据。
 *   两者在 Android 上是完全不同的 API，谁也不能替代谁，所以 App 里两套都留着，
 *   点设备时按 BluetoothDevice.getType() 自动选（和"混合蓝牙调试助手"的做法一致）。
 *
 * 数据通道的通用规律（自动探测，不用手填 UUID）：
 *   · 找同时支持 WRITE 和 NOTIFY 的特征值；优先常见串口 UUID（FFE1 / FFF1 / FFE2 / FFF2）
 *   · 写 CCCD(0x2902) 打开通知，之后的 onCharacteristicChanged 就是模块发来的数据
 *   · 发送时按 20 字节分包（未协商 MTU 时 BLE 单包上限就是 20 字节）
 *
 * 所有回调都在 Binder 线程触发，调用方需自行切主线程（复用 BluetoothSppManager.Listener）。
 */
@Suppress("DEPRECATION", "OVERRIDE_DEPRECATION")
class BleSppManager(private val context: Context) {

    /** 复用经典蓝牙那套监听接口，MainActivity 一份实现两套通吃 */
    var listener: BluetoothSppManager.Listener? = null

    private val adapter: BluetoothAdapter? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private var gatt: BluetoothGatt? = null
    private var writeChar: BluetoothGattCharacteristic? = null
    private var notifyChar: BluetoothGattCharacteristic? = null
    private var target: BluetoothDevice? = null

    /** GATT 全部就绪（连上 + 服务发现 + 通知打开）后才算可用 */
    @Volatile
    private var ready = false

    @Volatile
    private var disconnectNotified = true

    private val writeLock = Any()
    private val parser = FrameParser { payload -> listener?.onFrame(payload) }

    val isSupported: Boolean get() = adapter != null
    val isEnabled: Boolean get() = adapter?.isEnabled == true
    val isConnected: Boolean get() = ready

    // ------------------------------------------------------------------ 连接

    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        closeGatt(null)
        disconnectNotified = false
        target = device
        listener?.onConnecting(device)
        diag("BLE 连接开始：${device.address}（GATT 直连，已配对设备不用先扫描）")

        try {
            gatt = device.connectGatt(context, false, callback, BluetoothDevice.TRANSPORT_LE)
        } catch (e: SecurityException) {
            listener?.onError("缺少蓝牙权限（BLUETOOTH_CONNECT）")
            gatt = null
        } catch (e: Exception) {
            listener?.onError("BLE 连接失败：${e.message}")
            gatt = null
        }

        if (gatt == null) {
            disconnectNotified = true
            listener?.onError(
                "BLE 连接无法建立 —— 多半是模块被别的 App 占着（BLE 模块同样只允许一个连接），" +
                    "把别的蓝牙调试 App 断开或杀进程后重试"
            )
        }
    }

    private val callback = object : BluetoothGattCallback() {

        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    diag("BLE 链路已建立，开始发现服务…")
                    if (!g.discoverServices()) {
                        listener?.onError("BLE 服务发现启动失败")
                        closeGatt("服务发现启动失败")
                    }
                }

                BluetoothProfile.STATE_DISCONNECTED -> {
                    val reason = when {
                        status == BluetoothGatt.GATT_SUCCESS -> null
                        status == 133 -> "GATT 133（模块被占用或信号太弱，断电重启模块后重试）"
                        else -> "GATT 状态码 $status"
                    }
                    closeGatt(reason)
                }
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                listener?.onError("BLE 发现服务失败：GATT 状态码 $status")
                closeGatt("发现服务失败")
                return
            }
            pickSerialChannel(g)
        }

        override fun onDescriptorWrite(
            g: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            status: Int
        ) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                ready = true
                diag("BLE 通知已打开，链路就绪 ✓")
                listener?.onConnected(target ?: g.device)
            } else {
                listener?.onError("BLE 打开通知失败：GATT 状态码 $status")
                closeGatt("打开通知失败")
            }
        }

        /** Android 13(API 33) 起走这个新回调 */
        override fun onCharacteristicChanged(
            g: BluetoothGatt,
            c: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            parser.feed(value, value.size)
        }

        /** Android 12 及以下走这个旧回调 */
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            // API 33+ 会走上面的新回调，这里只兜住老系统，避免重复喂数据
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) return
            val v = c.value ?: return
            parser.feed(v, v.size)
        }
    }

    // ------------------------------------------------------------------ 找串口通道

    @SuppressLint("MissingPermission")
    private fun pickSerialChannel(g: BluetoothGatt) {
        val services: List<BluetoothGattService> = g.services ?: emptyList()
        val all = ArrayList<Pair<BluetoothGattService, BluetoothGattCharacteristic>>()
        for (s in services) {
            for (c in s.characteristics) all.add(s to c)
        }

        // 把探测到的服务/特征值全打进日志，万一自动选错，用户截图发我就能硬编码
        diag("BLE 发现 ${services.size} 个服务：")
        for (s in services) {
            diag("   服务 ${short(s.uuid)}  (${s.uuid})")
            for (c in s.characteristics) {
                diag("      特征 ${short(c.uuid)}  属性=${propText(c.properties)}")
            }
        }

        var w: BluetoothGattCharacteristic? = null
        var n: BluetoothGattCharacteristic? = null

        // ① 常见 BLE 串口特征值
        for (s in listOf("ffe1", "fff1", "ffe2", "fff2", "ffe4", "ff01")) {
            val hit = all.firstOrNull { short(it.second.uuid) == s } ?: continue
            if (writable(hit.second)) w = hit.second
            if (notifiable(hit.second)) n = hit.second
            if (w != null || n != null) break
        }

        // ② 同一个特征值既能写又能通知（最常见）
        if (w == null || n == null) {
            val both = all.firstOrNull { writable(it.second) && notifiable(it.second) }
            if (both != null) {
                if (w == null) w = both.second
                if (n == null) n = both.second
            }
        }

        // ③ 退而求其次：随便挑一个能写的 + 一个能通知的
        if (w == null) w = all.firstOrNull { writable(it.second) }?.second
        if (n == null) n = all.firstOrNull { notifiable(it.second) }?.second

        if (w == null || n == null) {
            val msg = "没找到可用的串口特征值（能写=${w != null} 能通知=${n != null}）。" +
                "请把上面的服务清单截图发我，我按你模块的 UUID 精确适配。"
            listener?.onError(msg)
            closeGatt("无可用串口特征值")
            return
        }

        writeChar = w
        notifyChar = n
        diag("选中通道：写=${short(w.uuid)}  通知=${short(n.uuid)}")

        // MTU 协商失败也不影响（发送侧本来按 20 字节分包）
        try {
            g.requestMtu(247)
        } catch (_: Exception) {
        }
        enableNotify(g, n)
    }

    @SuppressLint("MissingPermission")
    private fun enableNotify(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
        if (!g.setCharacteristicNotification(c, true)) {
            listener?.onError("打开本地通知失败（特征 ${short(c.uuid)}）")
            closeGatt("打开通知失败")
            return
        }
        val cccd = c.getDescriptor(CCCD_UUID)
        if (cccd == null) {
            // 少数模块没给 CCCD，那就直接当成就绪
            diag("该特征值没有 CCCD 描述符，直接视为就绪")
            ready = true
            listener?.onConnected(target ?: g.device)
            return
        }
        cccd.value = if (c.properties and BluetoothGattCharacteristic.PROPERTY_INDICATE != 0) {
            BluetoothGattDescriptor.ENABLE_INDICATION_VALUE
        } else {
            BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        }
        if (!g.writeDescriptor(cccd)) {
            listener?.onError("写 CCCD 失败（模块可能已断开）")
            closeGatt("写 CCCD 失败")
        }
    }

    // ------------------------------------------------------------------ 发送

    /**
     * 发送一帧。BLE 单包上限 20 字节（未协商 MTU 时），所以按 20 字节分包。
     * 与经典蓝牙一致：不追加 \r\n。
     */
    @SuppressLint("MissingPermission")
    fun send(frame: String): Boolean {
        val g = gatt
        val c = writeChar
        if (g == null || c == null || !ready) {
            listener?.onError("尚未连接车库蓝牙")
            return false
        }
        val data = frame.toByteArray(Charsets.US_ASCII)

        return try {
            synchronized(writeLock) {
                var offset = 0
                while (offset < data.size) {
                    val len = minOf(BLE_CHUNK, data.size - offset)
                    val part = data.copyOfRange(offset, offset + len)
                    c.writeType =
                        if (c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0) {
                            BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                        } else {
                            BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
                        }
                    c.value = part
                    if (!g.writeCharacteristic(c)) {
                        listener?.onError("BLE 发送失败：writeCharacteristic 返回 false")
                        return false
                    }
                    offset += len
                    if (offset < data.size) Thread.sleep(20)
                }
            }
            true
        } catch (e: Exception) {
            listener?.onError("BLE 发送异常：${e.message}")
            false
        }
    }

    // ------------------------------------------------------------------ 断开

    fun disconnect() {
        if (gatt == null) notifyDisconnected(null) else closeGatt(null)
    }

    /** App 退出时调用 */
    fun destroy() = closeGatt(null)

    @SuppressLint("MissingPermission")
    private fun closeGatt(reason: String?) {
        ready = false
        writeChar = null
        notifyChar = null
        val g = gatt
        gatt = null
        try {
            g?.disconnect()
        } catch (_: Exception) {
        }
        try {
            g?.close()
        } catch (_: Exception) {
        }
        notifyDisconnected(reason)
    }

    private fun notifyDisconnected(reason: String?) {
        if (disconnectNotified) return
        disconnectNotified = true
        parser.reset()
        listener?.onDisconnected(reason)
    }

    // ------------------------------------------------------------------ 工具

    private fun short(u: UUID): String =
        u.toString().substring(4, 8).lowercase()

    private fun writable(c: BluetoothGattCharacteristic): Boolean =
        c.properties and (
            BluetoothGattCharacteristic.PROPERTY_WRITE or
                BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE
            ) != 0

    private fun notifiable(c: BluetoothGattCharacteristic): Boolean =
        c.properties and (
            BluetoothGattCharacteristic.PROPERTY_NOTIFY or
                BluetoothGattCharacteristic.PROPERTY_INDICATE
            ) != 0

    private fun propText(p: Int): String {
        val sb = StringBuilder()
        if (p and BluetoothGattCharacteristic.PROPERTY_READ != 0) sb.append("读 ")
        if (p and BluetoothGattCharacteristic.PROPERTY_WRITE != 0) sb.append("写 ")
        if (p and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0) sb.append("写(无应答) ")
        if (p and BluetoothGattCharacteristic.PROPERTY_NOTIFY != 0) sb.append("通知 ")
        if (p and BluetoothGattCharacteristic.PROPERTY_INDICATE != 0) sb.append("指示 ")
        return if (sb.isEmpty()) "无" else sb.toString().trim()
    }

    private fun diag(message: String) = listener?.onDiagnostic("BLE " + message)

    private companion object {
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB")

        /** 未协商 MTU 时，BLE 单包有效载荷上限是 20 字节 */
        const val BLE_CHUNK = 20
    }
}
