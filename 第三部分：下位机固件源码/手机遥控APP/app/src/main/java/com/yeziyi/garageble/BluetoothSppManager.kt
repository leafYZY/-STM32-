package com.yeziyi.garageble

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothSocket
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.content.ContextCompat
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream

/**
 * 经典蓝牙（Bluetooth Classic SPP / RFCOMM）连接管理器。
 *
 * 为什么不用 Web Bluetooth / BluetoothGatt？
 *   HC-05 / HC-06 是蓝牙 2.x 经典模块，只提供 SPP 串口服务，不是 BLE，
 *   所以必须走 BluetoothSocket + RFCOMM，手机浏览器里的 Web Bluetooth 接不了。
 *
 * 所有回调都在【后台线程】触发，调用方需要自己 post 到主线程更新 UI。
 */
class BluetoothSppManager(private val context: Context) {

    interface Listener {
        /** 适配器开关状态变化 */
        fun onAdapterState(on: Boolean) {}

        /** 扫描到一个设备 */
        fun onDeviceFound(device: BluetoothDevice) {}

        /** 扫描结束 */
        fun onDiscoveryFinished() {}

        /** 开始连接（用于把状态条切成"正在连接"） */
        fun onConnecting(device: BluetoothDevice) {}

        /** 连接成功 */
        fun onConnected(device: BluetoothDevice) {}

        /** 连接断开（reason 为 null 表示对端主动断开/正常关闭） */
        fun onDisconnected(reason: String?) {}

        /** 收到一条完整帧的载荷（已去掉 '@' 和 '#*'） */
        fun onFrame(payload: String) {}

        /** 出错提示 */
        fun onError(message: String) {}

        /** 连接过程的逐步诊断信息（写进运行日志，方便定位是哪一环失败） */
        fun onDiagnostic(message: String) {}
    }

    var listener: Listener? = null

    private val adapter: BluetoothAdapter? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    private var socket: BluetoothSocket? = null
    private var readThread: Thread? = null
    private var connectedDevice: BluetoothDevice? = null
    private val writeLock = Any()
    private val parser = FrameParser { payload -> listener?.onFrame(payload) }

    /** 防止 onDisconnected 被重复回调 */
    @Volatile
    private var disconnectNotified = true

    val isSupported: Boolean get() = adapter != null
    val isEnabled: Boolean get() = adapter?.isEnabled == true
    val isConnected: Boolean get() = socket?.isConnected == true
    val isDiscovering: Boolean get() = adapter?.isDiscovering == true
    val deviceName: String get() = safeName(connectedDevice)
    val deviceAddress: String? get() = connectedDevice?.address

    @SuppressLint("MissingPermission")
    fun bondedDevices(): List<BluetoothDevice> =
        try {
            adapter?.bondedDevices?.toList() ?: emptyList()
        } catch (e: SecurityException) {
            listener?.onError("缺少蓝牙权限（BLUETOOTH_CONNECT）")
            emptyList()
        }

    // ------------------------------------------------------------------ 广播

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context?, intent: Intent?) {
            when (intent?.action) {
                BluetoothDevice.ACTION_FOUND -> {
                    @Suppress("DEPRECATION")
                    val d = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
                    if (d != null) listener?.onDeviceFound(d)
                }

                BluetoothAdapter.ACTION_DISCOVERY_FINISHED ->
                    listener?.onDiscoveryFinished()

                BluetoothAdapter.ACTION_STATE_CHANGED -> {
                    val st = intent.getIntExtra(
                        BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR
                    )
                    if (st == BluetoothAdapter.STATE_ON) listener?.onAdapterState(true)
                    if (st == BluetoothAdapter.STATE_OFF) {
                        listener?.onAdapterState(false)
                        closeSocket("蓝牙已关闭")
                    }
                }

                BluetoothDevice.ACTION_BOND_STATE_CHANGED -> {
                    @Suppress("DEPRECATION")
                    val d = intent.getParcelableExtra<BluetoothDevice>(BluetoothDevice.EXTRA_DEVICE)
                    if (d != null) listener?.onDeviceFound(d)
                }
            }
        }
    }

    private var registered = false

    fun start() {
        if (registered) return
        val filter = IntentFilter().apply {
            addAction(BluetoothDevice.ACTION_FOUND)
            addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED)
            addAction(BluetoothAdapter.ACTION_STATE_CHANGED)
            addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED)
        }
        ContextCompat.registerReceiver(
            context, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED
        )
        registered = true
    }

    fun stop() {
        if (registered) {
            try {
                context.unregisterReceiver(receiver)
            } catch (_: IllegalArgumentException) {
                // ignore
            }
            registered = false
        }
    }

    // ------------------------------------------------------------------ 扫描

    @SuppressLint("MissingPermission")
    fun startDiscovery(): Boolean {
        val a = adapter ?: return false
        if (!a.isEnabled) {
            listener?.onError("请先在系统设置里打开蓝牙")
            return false
        }
        return try {
            if (a.isDiscovering) a.cancelDiscovery()
            a.startDiscovery()
        } catch (e: SecurityException) {
            listener?.onError("缺少蓝牙扫描权限")
            false
        }
    }

    @SuppressLint("MissingPermission")
    fun cancelDiscovery() {
        try {
            adapter?.cancelDiscovery()
        } catch (_: SecurityException) {
            // ignore
        }
    }

    // ------------------------------------------------------------------ 连接

    /**
     * 连接车库蓝牙。
     *
     * HC-05 这类老模块建链失败的花样很多（典型报错 read failed, socket might closed
     * or timeout, read ret: -1），所以这里做了三层保险：
     *   1. 三种建链方式依次尝试：安全通道 → 非安全通道 → 反射 channel 1；
     *   2. 每个 socket 都有看门狗（[CONNECT_TIMEOUT_MS]），超时就从另一个线程 close
     *      强行解开阻塞，避免一次连接卡 30 秒以上；
     *   3. 整条阶梯重试 [MAX_ATTEMPTS] 轮，中间隔一会儿让模块喘口气。
     */
    @SuppressLint("MissingPermission")
    fun connect(device: BluetoothDevice) {
        if (isConnected && connectedDevice?.address == device.address) return

        closeSocket(null)   // 先彻底清掉旧连接
        disconnectNotified = false
        listener?.onConnecting(device)

        Thread {
            try {
                adapter?.cancelDiscovery()
            } catch (_: SecurityException) {
            }
            // 刚取消扫描时立刻建链成功率很低，等一下下
            sleepQuietly(START_DELAY_MS)

            diag(
                "开始建链：${device.address} 类型=${typeText(device)}" +
                    " 配对状态=${bondStateText(device)}" +
                    " 适配器=${if (isEnabled) "已开启" else "未开启"}" +
                    " 共 $MAX_ATTEMPTS 轮 × 3 种方式"
            )

            // ★ 关键判定：纯 BLE 设备用经典蓝牙 SPP 是【原理上】连不上的，
            //   立刻给结论，不做无意义的 6 次重试。
            if (isBleOnly(device)) {
                listener?.onError(
                    "这个设备是 BLE（低功耗蓝牙），不是经典蓝牙。" +
                        "HC-05 用的是经典蓝牙 SPP，两者不是一回事，所以怎么连都会被拒。" +
                        "如果你的模块是 HM-10 / AT-09 / JDY-xx 这类 BLE 串口模块，" +
                        "告诉我，我给你写 BLE 版本的 APP。"
                )
                disconnectNotified = true
                return@Thread
            }

            var s: BluetoothSocket? = null
            var last: Exception? = null

            for (attempt in 1..MAX_ATTEMPTS) {
                if (attempt > 1) sleepQuietly(RETRY_GAP_MS)
                val sock = tryConnectOnce(device, attempt) { last = it }
                if (sock != null) {
                    s = sock
                    break
                }
            }

            if (s == null) {
                diag("建链失败：3 种方式 × $MAX_ATTEMPTS 轮全部被拒")
                listener?.onError(explainConnectFailure(last))
                // 从来没连上过，不要误报「连接已断开」
                disconnectNotified = true
                return@Thread
            }

            socket = s
            connectedDevice = device
            parser.reset()
            listener?.onConnected(device)
            readLoop(s)
        }.also { it.name = "spp-connect" }.start()
    }

    /** 一轮建链：安全通道 → 非安全通道 → 反射 channel 1 */
    @SuppressLint("MissingPermission")
    private fun tryConnectOnce(
        device: BluetoothDevice,
        round: Int,
        report: (Exception) -> Unit
    ): BluetoothSocket? {
        val tag = "[$round/$MAX_ATTEMPTS]"
        val factories: List<Pair<String, () -> BluetoothSocket>> = listOf(
            "安全通道" to { device.createRfcommSocketToServiceRecord(Protocol.SPP_UUID) },
            "非安全通道" to {
                device.createInsecureRfcommSocketToServiceRecord(Protocol.SPP_UUID)
            },
            "channel 1 兜底" to {
                createChannel1Socket(device)
                    ?: throw IOException("反射创建失败（可能是系统限制隐藏 API）")
            }
        )

        for ((name, factory) in factories) {
            val sock = try {
                factory()
            } catch (e: Exception) {
                diag("$tag $name 创建 socket 失败 → ${shortError(e)}")
                report(e)
                continue
            }
            val err = connectWithWatchdog(sock)
            if (err == null && sock.isConnected) {
                diag("$tag $name 建链成功 ✓")
                return sock
            }
            diag("$tag $name 被拒 → ${shortError(err)}")
            report(err ?: IOException("$name 建链后立刻断开"))
            try {
                sock.close()
            } catch (_: Exception) {
            }
        }
        return null
    }

    @SuppressLint("MissingPermission")
    private fun bondStateText(device: BluetoothDevice): String = try {
        when (device.bondState) {
            BluetoothDevice.BOND_BONDED -> "已配对"
            BluetoothDevice.BOND_BONDING -> "配对中"
            else -> "未配对"
        }
    } catch (_: SecurityException) {
        "未知"
    }

    /**
     * 设备类型：HC-05 / HC-06 必须是【经典蓝牙】。
     * 市面上有不少顶着"HC-05"名字卖的其实是 BLE 模块（HM-10 / AT-09 / CC2541 克隆板），
     * 那种用 RFCOMM 是永远连不上的。
     */
    @SuppressLint("MissingPermission")
    private fun typeText(device: BluetoothDevice): String = try {
        when (device.type) {
            BluetoothDevice.DEVICE_TYPE_CLASSIC -> "经典蓝牙(BR/EDR)"
            BluetoothDevice.DEVICE_TYPE_LE -> "BLE(低功耗)"
            BluetoothDevice.DEVICE_TYPE_DUAL -> "双模"
            else -> "未知"
        }
    } catch (_: SecurityException) {
        "未知"
    }

    /** 纯 BLE 设备（dual 不拦，它还能走经典通道） */
    @SuppressLint("MissingPermission")
    private fun isBleOnly(device: BluetoothDevice): Boolean = try {
        device.type == BluetoothDevice.DEVICE_TYPE_LE
    } catch (_: SecurityException) {
        false
    }

    /** 只保留报错的核心信息，日志里别太长 */
    private fun shortError(e: Exception?): String {
        val m = e?.message?.trim().orEmpty()
        return if (m.isEmpty()) e?.javaClass?.simpleName ?: "未知错误" else m
    }

    private fun diag(message: String) {
        listener?.onDiagnostic(message)
    }

    /** 带看门狗的 connect()：超时就从另一个线程 close()，强行解开阻塞 */
    private fun connectWithWatchdog(sock: BluetoothSocket): Exception? {
        val watchdog = Thread {
            try {
                Thread.sleep(CONNECT_TIMEOUT_MS)
                try {
                    sock.close()
                } catch (_: Exception) {
                }
            } catch (_: InterruptedException) {
                // 正常结束
            }
        }
        watchdog.isDaemon = true
        watchdog.start()
        return try {
            sock.connect()
            if (sock.isConnected) null else IOException("连接建立后未保持")
        } catch (e: Exception) {
            e
        } finally {
            watchdog.interrupt()
        }
    }

    /**
     * HC-05 等老模块的兜底建链方式：反射调用隐藏的 createRfcommSocket(channel)。
     * 不少手机用标准 SPP UUID 建链会被模块拒掉（read ret: -1），换 channel 1 往往就通了。
     */
    private fun createChannel1Socket(device: BluetoothDevice): BluetoothSocket? =
        try {
            val m = device.javaClass.getMethod(
                "createRfcommSocket", Int::class.javaPrimitiveType
            )
            m.invoke(device, 1) as? BluetoothSocket
        } catch (_: Throwable) {
            null
        }

    private fun sleepQuietly(ms: Long) {
        try {
            Thread.sleep(ms)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        }
    }

    /** 把底层报错翻译成用户能照着做的提示 */
    private fun explainConnectFailure(e: Exception?): String {
        val raw = e?.message.orEmpty()
        return when {
            raw.contains("read failed", true) || raw.contains("ret: -1", true) ->
                "连接被拒绝 —— ① 先关掉实体遥控器（HC-05 同时只能连一个主设备）；" +
                    "② 把 HC-05 断电重新上电（别停在 AT 模式）；" +
                    "③ 系统蓝牙里取消配对后重新配对；④ 靠近模块 1 米内再试"

            raw.contains("service discovery", true) || raw.contains("SDP", true) ->
                "找不到串口服务(SDP)：配对记录可能过期 —— 系统设置里「取消配对」再重新配对（密码 123456）"

            raw.contains("permission", true) ->
                "缺少蓝牙权限：设置 → 应用 → 车库蓝牙助手 → 权限，允许「附近的设备」"

            raw.isBlank() ->
                "连接失败：模块没有响应，请确认 HC-05 已通电、指示灯在闪"

            else -> "连接失败：$raw"
        }
    }

    /** 读取循环：放在独立线程里，阻塞式 read() */
    private fun readLoop(s: BluetoothSocket) {
        readThread = Thread {
            val buf = ByteArray(512)
            var reason: String? = null
            try {
                val ins: InputStream = s.inputStream
                while (true) {
                    val n = ins.read(buf)
                    if (n < 0) {
                        reason = "对端已断开"
                        break
                    }
                    if (n > 0) parser.feed(buf, n)
                }
            } catch (e: Exception) {
                reason = e.message
            } finally {
                if (socket === s) {
                    socket = null
                    connectedDevice = null
                    notifyDisconnected(reason)
                }
            }
        }.also {
            it.name = "spp-read"
            it.start()
        }
    }

    // ------------------------------------------------------------------ 发送

    /**
     * 发送一帧。返回值表示是否成功写入输出流。
     * 注意：这里【不】自动追加 \r\n —— 车库端状态机靠 '@' 和 '#*' 定界，附加换行没必要。
     */
    fun send(frame: String): Boolean {
        val s = socket ?: run {
            listener?.onError("尚未连接车库蓝牙")
            return false
        }
        return try {
            synchronized(writeLock) {
                val out: OutputStream = s.outputStream
                out.write(frame.toByteArray(Charsets.US_ASCII))
                out.flush()
            }
            true
        } catch (e: Exception) {
            listener?.onError("发送失败：${e.message}")
            closeSocket("发送异常")
            false
        }
    }

    // ------------------------------------------------------------------ 断开

    fun disconnect() {
        if (socket == null) {
            notifyDisconnected(null)
        } else {
            closeSocket(null)
        }
    }

    private fun closeSocket(reason: String?) {
        val s = socket
        socket = null
        connectedDevice = null
        try {
            s?.close()
        } catch (_: Exception) {
        }
        readThread = null
        notifyDisconnected(reason)
    }

    private fun notifyDisconnected(reason: String?) {
        if (disconnectNotified) return
        disconnectNotified = true
        parser.reset()
        listener?.onDisconnected(reason)
    }

    @SuppressLint("MissingPermission")
    private fun safeName(d: BluetoothDevice?): String =
        try {
            d?.name ?: "未知设备"
        } catch (_: SecurityException) {
            "未知设备"
        }

    /** 供 Activity 检查权限用 */
    companion object {
        /** 整条建链阶梯重试几轮 */
        private const val MAX_ATTEMPTS = 2

        /** 每轮之间的间隔，给 HC-05 一点恢复时间 */
        private const val RETRY_GAP_MS = 1200L

        /** 取消扫描后、开始建链前的等待（HC-05 反应慢，太急必被拒） */
        private const val START_DELAY_MS = 800L

        /** 单个 socket 的建链超时（HC-05 正常 1~3 秒就能连上） */
        private const val CONNECT_TIMEOUT_MS = 6000L

        fun requiredPermissions(): Array<String> =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                arrayOf(
                    Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT
                )
            } else {
                arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
            }

        fun hasPermissions(context: Context): Boolean =
            requiredPermissions().all {
                ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
            }
    }
}
