package com.yeziyi.garageble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.content.Context
import android.content.Intent
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.View
import android.widget.SeekBar
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.yeziyi.garageble.databinding.ActivityMainBinding
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * 手机蓝牙遥控车库系统 —— 界面逻辑
 *
 * 通信完全等价于「蓝牙遥控器5」工程：把 STM32 遥控器发的那几帧 ASCII
 * 原封不动地通过 HC-05 发给车库，所以车库固件一行都不用为了"兼容 APP"而改动
 * （只有新增的 @Q#* 状态查询需要打补丁）。
 */
class MainActivity : AppCompatActivity(), BluetoothSppManager.Listener {

    private lateinit var b: ActivityMainBinding
    private lateinit var bt: BluetoothSppManager

    /**
     * BLE 通道。两条链路只会激活一条：
     *   · 经典蓝牙（HC-05/06）→ [bt]，走 RFCOMM/SPP
     *   · BLE 模块（HM-10/AT-09/JDY）→ [ble]，走 GATT
     * 点设备时按 BluetoothDevice.getType() 自动选择，界面与指令完全共用。
     */
    private var ble: BleSppManager? = null
    private var bleMode = false

    private val deviceAdapter = DeviceAdapter { onDevicePicked(it) }
    private val logAdapter = LogAdapter()
    private val recordAdapter = RecordAdapter()

    private val records = ArrayList<ParkingRecord>()
    private val ui = Handler(Looper.getMainLooper())

    private var showUnbonded = true
    /** 程序化改动开关 / 模式按钮时，抑制回调，避免"回灌"成一次发送 */
    private var suppressUiCallback = false
    private var logCollapsed = false

    private val prefs by lazy { getSharedPreferences(PREFS, Context.MODE_PRIVATE) }

    private val clockFormat = SimpleDateFormat("HH:mm:ss", Locale.US)

    /** 当前链路是否已就绪（自动分辨经典 / BLE） */
    private val isLinked: Boolean
        get() = if (bleMode) ble?.isConnected == true else bt.isConnected

    private fun linkSend(frame: String): Boolean =
        if (bleMode) ble?.send(frame) == true else bt.send(frame)

    private fun linkDisconnect() {
        if (bleMode) ble?.disconnect() else bt.disconnect()
    }

    // ---- 状态查询自检：用来区分「APP 没发」「车库没回」这两种情况 ----
    private var pollSent = 0
    private var statusSeen = false
    private var statusWarned = false

    /** 连接后每 2 秒轮询一次车库状态 */
    private val pollStatus = object : Runnable {
        override fun run() {
            if (!isLinked) return

            if (pollSent == 0) {
                logLine(LogAdapter.Kind.SYS, "开始每 2 秒查询车库状态：${Protocol.queryStatus()}")
            }
            pollSent++
            val ok = linkSend(Protocol.queryStatus())

            // 只要还没收到过状态，就把每次发送都记下来 ——
            // 这样一眼能分辨是「APP 没发出去」还是「车库没回」。
            if (!statusSeen) {
                logLine(
                    LogAdapter.Kind.TX,
                    "查询状态 → ${Protocol.queryStatus()}" + if (ok) "" else "（发送失败）"
                )
            }

            // 发了几次都没回音 → 基本可以断定车库固件里没有 @Q#* 分支
            if (!statusSeen && !statusWarned && pollSent >= 3) {
                statusWarned = true
                b.tvLastSync.text = "车库未回复状态查询 · 需要给固件烧录 @Q#* 补丁"
                logLine(
                    LogAdapter.Kind.ERR,
                    "已连上且发出 $pollSent 次 @Q#*，但车库一直没有回复。" +
                        "开闸/关闸能回说明通信正常，所以原因是：**车库固件里的 @Q#* 补丁还没烧录**。" +
                        "请在 Keil 里 Rebuild 后重新下载到 STM32（只改源码不烧录不生效）。"
                )
            }
            ui.postDelayed(this, POLL_INTERVAL_MS)
        }
    }

    private fun resetPollWatchdog() {
        pollSent = 0
        statusSeen = false
        statusWarned = false
    }

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) { result ->
        if (result.values.all { it }) {
            logLine(LogAdapter.Kind.SYS, "蓝牙权限已授予，开始扫描")
            doStartDiscovery()
        } else {
            logLine(LogAdapter.Kind.ERR, "未授予蓝牙权限，无法搜索/连接设备")
            toast("需要蓝牙权限才能连接车库")
        }
    }

    // ================================================================ 生命周期

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        b = ActivityMainBinding.inflate(layoutInflater)
        setContentView(b.root)

        bt = BluetoothSppManager(applicationContext)
        bt.listener = this
        ble = BleSppManager(applicationContext)
        ble?.listener = this

        setupLists()
        setupToolbar()
        setupControls()
        loadRecords()
        applyConnectedUi(false)

        logLine(LogAdapter.Kind.SYS, "车库蓝牙助手已启动 · 协议 @命令#* · 经典蓝牙 + BLE 双通道")
        b.tvScanHint.text = getString(R.string.hint_scan)
    }

    override fun onStart() {
        super.onStart()
        bt.start()
    }

    override fun onStop() {
        super.onStop()
        bt.cancelDiscovery()
        bt.stop()
    }

    override fun onDestroy() {
        super.onDestroy()
        ui.removeCallbacks(pollStatus)
        if (bleMode) ble?.destroy() else bt.disconnect()
        bt.listener = null
        ble?.listener = null
        ble = null
    }

    // ================================================================ 初始化

    private fun setupLists() {
        b.listDevices.layoutManager = LinearLayoutManager(this)
        b.listDevices.adapter = deviceAdapter

        b.listRecords.layoutManager = LinearLayoutManager(this)
        b.listRecords.adapter = recordAdapter

        b.listLog.layoutManager = LinearLayoutManager(this)
        b.listLog.adapter = logAdapter
    }

    private fun setupToolbar() {
        b.toolbar.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                R.id.action_scan -> {
                    startScan()
                    true
                }

                R.id.action_show_unbonded -> {
                    item.isChecked = !item.isChecked
                    showUnbonded = item.isChecked
                    toast(if (showUnbonded) "将一并显示未配对设备" else "只显示已配对设备")
                    doStartDiscovery()
                    true
                }

                R.id.action_disconnect -> {
                    linkDisconnect()
                    true
                }

                R.id.action_about -> {
                    showAbout()
                    true
                }

                else -> false
            }
        }
    }

    private fun setupControls() {
        // 开闸 / 关闸
        b.btnOpenGate.setOnClickListener {
            sendCommand(Protocol.openGate(), "开闸")
        }
        b.btnCloseGate.setOnClickListener {
            sendCommand(Protocol.closeGate(), "关闸")
        }

        // 工作模式切换
        b.toggleMode.addOnButtonCheckedListener { _, checkedId, isChecked ->
            if (!isChecked || suppressUiCallback) return@addOnButtonCheckedListener
            when (checkedId) {
                R.id.btnModeAuto -> sendCommand(Protocol.modeAuto(), "切换自动模式")
                R.id.btnModeManual -> sendCommand(Protocol.modeManual(), "切换手动模式")
            }
        }

        // 音量滑条：拖动时只预览，松手才发送（避免刷屏）
        b.seekVolume.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar?, progress: Int, fromUser: Boolean) {
                val v = snapVolume(progress)
                b.tvVolume.text = "$v %"
                b.tvVolumeHint.text = "松手即发送 ${Protocol.setVolume(v)}"
            }

            override fun onStartTrackingTouch(sb: SeekBar?) = Unit

            override fun onStopTrackingTouch(sb: SeekBar?) {
                val v = snapVolume(sb?.progress ?: 0)
                if (b.seekVolume.progress != v) b.seekVolume.progress = v
                b.tvVolume.text = "$v %"
                sendCommand(Protocol.setVolume(v), "设置语音音量 $v%")
            }
        })

        // 6 路车位检测器
        sensorSwitches().forEachIndexed { i, sw ->
            val index = i + 1
            sw.setOnCheckedChangeListener { _, checked ->
                if (suppressUiCallback) return@setOnCheckedChangeListener
                sendCommand(
                    Protocol.setSensor(index, checked),
                    "${Protocol.spaceText(index)} ${if (checked) "启用" else "禁用"}"
                )
            }
        }

        b.btnSensorAllOn.setOnClickListener { setAllSensors(true) }
        b.btnSensorAllOff.setOnClickListener { setAllSensors(false) }

        // 停车记录清空（只清本机显示，SD 卡里的记录请用遥控器"清除记录"菜单）
        b.btnClearRecords.setOnClickListener {
            if (records.isEmpty()) {
                toast("当前没有记录")
                return@setOnClickListener
            }
            MaterialAlertDialogBuilder(this)
                .setTitle("清空本机记录？")
                .setMessage("仅清除手机上的显示缓存，不会删除遥控器 SD 卡中的停车记录。")
                .setNegativeButton("取消", null)
                .setPositiveButton("清空") { _, _ ->
                    records.clear()
                    saveRecords()
                    renderRecords()
                    logLine(LogAdapter.Kind.SYS, "已清空本机停车记录")
                }
                .show()
        }

        // 日志：清空 / 折叠
        b.btnClearLog.setOnClickListener {
            logAdapter.clear()
            b.tvLogCount.text = "0 行（点击折叠）"
        }
        b.logHeader.setOnClickListener {
            logCollapsed = !logCollapsed
            b.listLog.visibility = if (logCollapsed) View.GONE else View.VISIBLE
            b.tvLogCount.text =
                "${logAdapter.size} 行（点击${if (logCollapsed) "展开" else "折叠"}）"
        }

        // 重连：优先连上次成功连接过的设备
        b.btnReconnect.setOnClickListener {
            val mac = prefs.getString(KEY_LAST_MAC, null)
            if (mac.isNullOrBlank()) {
                toast("还没有连接记录，请先搜索设备")
                startScan()
                return@setOnClickListener
            }
            val dev = bt.bondedDevices().firstOrNull { it.address == mac }
            if (dev == null) {
                toast("上次的设备已不在配对列表，请重新搜索")
                startScan()
            } else {
                logLine(LogAdapter.Kind.SYS, "重连 ${safeName(dev)} …")
                startLink(dev)
            }
        }
    }

    /**
     * 按设备类型选通道：BLE 走 GATT，经典蓝牙走 RFCOMM。
     * 判据与「混合蓝牙调试助手」一致（BluetoothDevice.getType()）。
     */
    @SuppressLint("MissingPermission")
    private fun isBleDevice(d: BluetoothDevice): Boolean =
        try {
            d.type == BluetoothDevice.DEVICE_TYPE_LE
        } catch (_: SecurityException) {
            false
        }

    @SuppressLint("MissingPermission")
    private fun startLink(device: BluetoothDevice) {
        if (isBleDevice(device)) {
            bleMode = true
            logLine(LogAdapter.Kind.SYS, "类型 = BLE 低功耗 → 走 GATT 通道")
            ble?.connect(device)
        } else {
            bleMode = false
            logLine(LogAdapter.Kind.SYS, "类型 = 经典蓝牙 → 走 SPP/RFCOMM 通道")
            bt.connect(device)
        }
    }

    private fun sensorSwitches() = listOf(
        b.swSensor1, b.swSensor2, b.swSensor3,
        b.swSensor4, b.swSensor5, b.swSensor6
    )

    private fun snapVolume(progress: Int): Int = (progress / 5 * 5).coerceIn(0, 100)

    // ================================================================ 扫描

    private fun requestPermissions() {
        permissionLauncher.launch(BluetoothSppManager.requiredPermissions())
    }

    @SuppressLint("MissingPermission")
    private fun startScan() {
        if (!bt.isSupported) {
            toast("本机不支持蓝牙")
            return
        }
        if (!bt.isEnabled) {
            logLine(LogAdapter.Kind.ERR, "蓝牙未开启，正在请求打开")
            if (BluetoothSppManager.hasPermissions(this)) {
                startActivity(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
            } else {
                requestPermissions()
            }
            return
        }
        if (!BluetoothSppManager.hasPermissions(this)) {
            requestPermissions()
            return
        }
        if (Build.VERSION.SDK_INT <= Build.VERSION_CODES.R && !isLocationEnabled()) {
            logLine(LogAdapter.Kind.ERR, "Android 12 以下扫描蓝牙必须打开系统「位置信息」总开关")
            toast("请先打开系统定位开关，否则搜不到设备")
        }
        doStartDiscovery()
    }

    @SuppressLint("MissingPermission")
    private fun doStartDiscovery() {
        showDevicePanel()
        val bonded = bt.bondedDevices()
        deviceAdapter.submit(bonded)
        b.tvEmptyDevices.visibility = if (bonded.isEmpty()) View.VISIBLE else View.GONE
        logLine(
            LogAdapter.Kind.SYS,
            "已列出 ${bonded.size} 个已配对设备，正在扫描附近设备…"
        )
        if (!bt.startDiscovery()) {
            logLine(LogAdapter.Kind.ERR, "启动扫描失败")
        }
    }

    private fun isLocationEnabled(): Boolean {
        val lm = getSystemService(Context.LOCATION_SERVICE) as? LocationManager
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            lm?.isLocationEnabled == true
        } else {
            @Suppress("DEPRECATION")
            Settings.Secure.getInt(
                contentResolver,
                Settings.Secure.LOCATION_MODE,
                Settings.Secure.LOCATION_MODE_OFF
            ) != Settings.Secure.LOCATION_MODE_OFF
        }
    }

    // ================================================================ 设备选择

    @SuppressLint("MissingPermission")
    private fun onDevicePicked(device: BluetoothDevice) {
        if (!BluetoothSppManager.hasPermissions(this)) {
            requestPermissions()
            return
        }
        when (device.bondState) {
            BluetoothDevice.BOND_BONDED -> {
                logLine(LogAdapter.Kind.SYS, "正在连接 ${safeName(device)} (${device.address}) …")
                toast("正在连接，请稍候…")
                startLink(device)
            }

            BluetoothDevice.BOND_BONDING ->
                toast("正在配对中，请稍候…")

            else -> {
                logLine(LogAdapter.Kind.SYS, "首次连接需配对，发起配对请求（PIN 码 123456）…")
                toast("请在弹窗中输入配对密码 123456")
                try {
                    device.createBond()
                } catch (e: SecurityException) {
                    logLine(LogAdapter.Kind.ERR, "发起配对失败：${e.message}")
                }
            }
        }
    }

    // ================================================================ 发送

    private fun sendCommand(frame: String, label: String) {
        if (!isLinked) {
            toast("尚未连接车库蓝牙")
            return
        }
        if (linkSend(frame)) {
            logLine(LogAdapter.Kind.TX, "$label  →  $frame")
        }
    }

    private fun setAllSensors(enable: Boolean) {
        suppressUiCallback = true
        sensorSwitches().forEach { it.isChecked = enable }
        suppressUiCallback = false
        if (!isLinked) {
            toast("尚未连接车库蓝牙，仅更新了界面")
            return
        }
        for (i in 1..6) {
            sendCommand(
                Protocol.setSensor(i, enable),
                "${Protocol.spaceText(i)} ${if (enable) "启用" else "禁用"}"
            )
        }
    }

    // ================================================================ 连接回调（后台线程）

    override fun onAdapterState(on: Boolean) {
        ui.post {
            logLine(LogAdapter.Kind.SYS, if (on) "蓝牙已打开" else "蓝牙已关闭")
            if (!on) applyConnectedUi(false)
        }
    }

    override fun onDeviceFound(device: BluetoothDevice) {
        ui.post {
            val before = deviceAdapter.itemCount
            deviceAdapter.add(device, showUnbonded)
            if (deviceAdapter.itemCount != before) {
                b.tvEmptyDevices.visibility = View.GONE
                logLine(
                    LogAdapter.Kind.SYS,
                    "发现设备：${safeName(device)} ${device.address}（点击连接）"
                )
            }
        }
    }

    override fun onDiscoveryFinished() {
        ui.post { logLine(LogAdapter.Kind.SYS, "扫描结束") }
    }

    override fun onConnecting(device: BluetoothDevice) {
        ui.post {
            b.tvStatusDot.setTextColor(ContextCompat.getColor(this, R.color.gate_wait))
            b.tvStatus.text = "正在连接 ${safeName(device)} …（最多约 20 秒）"
        }
    }

    override fun onConnected(device: BluetoothDevice) {
        ui.post {
            prefs.edit().putString(KEY_LAST_MAC, device.address).apply()
            applyConnectedUi(true, safeName(device))
            logLine(LogAdapter.Kind.SYS, "已连接 ${safeName(device)} (${device.address})")
            toast("已连接车库")
            resetPollWatchdog()
            ui.removeCallbacks(pollStatus)
            ui.postDelayed(pollStatus, 400)
        }
    }

    override fun onDisconnected(reason: String?) {
        ui.post {
            ui.removeCallbacks(pollStatus)
            resetPollWatchdog()
            applyConnectedUi(false)
            logLine(
                LogAdapter.Kind.ERR,
                "连接已断开" + if (reason.isNullOrBlank()) "" else "（$reason）"
            )
            b.tvLastSync.text = "连接已断开 · 点「重连」或重新搜索设备"
        }
    }

    override fun onError(message: String) {
        ui.post { logLine(LogAdapter.Kind.ERR, message) }
    }

    override fun onDiagnostic(message: String) {
        ui.post { logLine(LogAdapter.Kind.SYS, message) }
    }

    override fun onFrame(payload: String) {
        ui.post { handleFrame(payload) }
    }

    // ================================================================ 回复处理

    private fun handleFrame(payload: String) {
        logLine(LogAdapter.Kind.RX, "@${payload}#*   →   ${describeReply(payload)}")

        when (val reply = Protocol.parseReply(payload)) {
            is GarageReply.Ack -> {
                when (reply.action) {
                    GateAction.OPEN -> setGateText(2)
                    GateAction.CLOSE -> setGateText(0)
                    GateAction.MODE_AUTO -> setModeText(true)
                    GateAction.MODE_MANUAL -> setModeText(false)
                    else -> Unit
                }
            }

            is GarageReply.Status -> applyStatus(reply)

            is GarageReply.Parking -> {
                val rec = ParkingRecord(reply.plate, reply.hhmmss, reply.space)
                if (records.none { it.dedupKey() == rec.dedupKey() }) {
                    records.add(0, rec)
                    while (records.size > MAX_RECORDS) records.removeAt(records.size - 1)
                    saveRecords()
                    renderRecords()
                }
                logLine(
                    LogAdapter.Kind.RX,
                    "停车入库：${rec.plate}  ${rec.timeText}  ${rec.spaceText}"
                )
            }

            is GarageReply.Unknown -> Unit
        }
    }

    private fun describeReply(payload: String): String = when (payload) {
        "KOK" -> "开闸成功"
        "GOK" -> "关闸成功"
        "AOK" -> "已切换到自动模式"
        "MOK" -> "已切换到手动模式"
        "VOK" -> "音量设置成功"
        "SOK" -> "车位检测器设置成功"
        else -> if (payload.startsWith("Q")) "状态上报" else "车库回复"
    }

    private fun applyStatus(s: GarageReply.Status) {
        // 收到过状态 → 关掉"车库没回复"的自检告警
        statusSeen = true
        statusWarned = false

        setGateText(s.gate)
        setModeText(s.autoMode)
        b.tvFree.text = "${s.freeSpaces}"

        b.tvLastSync.text = "最后同步 ${clockFormat.format(Date())} · 每 2 秒自动刷新"

        // 同步模式按钮（抑制回调，避免又发一次模式指令）
        suppressUiCallback = true
        b.toggleMode.check(if (s.autoMode) R.id.btnModeAuto else R.id.btnModeManual)

        // 同步 6 路检测器开关
        val sw = sensorSwitches()
        for (i in 0 until sw.size) {
            val on = s.sensorMask.getOrNull(i) != '0'
            sw[i].isChecked = on
        }
        suppressUiCallback = false
    }

    private fun setGateText(gate: Int) {
        b.tvGate.text = Protocol.gateText(gate)
        val color = when (gate) {
            0 -> R.color.gate_close
            1 -> R.color.gate_wait
            2 -> R.color.gate_open
            else -> R.color.text_secondary
        }
        b.tvGate.setTextColor(ContextCompat.getColor(this, color))
    }

    private fun setModeText(auto: Boolean) {
        b.tvMode.text = if (auto) "自动" else "手动"
        b.tvMode.setTextColor(
            ContextCompat.getColor(this, if (auto) R.color.brand else R.color.gate_wait)
        )
    }

    // ================================================================ 停车记录

    private fun renderRecords() {
        recordAdapter.submit(records)
        b.tvRecordCount.text = "${records.size} 条"
        b.tvRecordEmpty.visibility = if (records.isEmpty()) View.VISIBLE else View.GONE
    }

    private fun saveRecords() {
        val text = records.joinToString("\n") { it.encode() }
        prefs.edit().putString(KEY_RECORDS, text).apply()
    }

    private fun loadRecords() {
        val text = prefs.getString(KEY_RECORDS, null).orEmpty()
        records.clear()
        if (text.isNotBlank()) {
            text.split("\n").forEach { line ->
                ParkingRecord.decode(line)?.let { records.add(it) }
            }
        }
        renderRecords()
        if (records.isNotEmpty()) {
            logLine(LogAdapter.Kind.SYS, "已恢复本机 ${records.size} 条停车记录")
        }
    }

    // ================================================================ UI 辅助

    private fun applyConnectedUi(connected: Boolean, name: String? = null) {
        b.panelDevices.visibility = if (connected) View.GONE else View.VISIBLE
        b.panelControl.visibility = if (connected) View.VISIBLE else View.GONE

        if (connected) {
            b.tvStatusDot.setTextColor(ContextCompat.getColor(this, R.color.ok_green))
            b.tvStatus.text = "已连接：${name ?: "车库"}"
        } else {
            b.tvStatusDot.setTextColor(ContextCompat.getColor(this, R.color.warn_red))
            b.tvStatus.text = getString(R.string.status_disconnected)
            b.tvGate.text = "—"
            b.tvMode.text = "—"
            b.tvFree.text = "—"
            b.tvLastSync.text = "尚未同步 · 连接后每 2 秒自动查询车库状态"
        }
    }

    private fun showDevicePanel() {
        b.panelDevices.visibility = View.VISIBLE
        b.panelControl.visibility = View.GONE
    }

    private fun logLine(kind: LogAdapter.Kind, msg: String) {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            logAdapter.add(kind, msg)
            b.tvLogCount.text =
                "${logAdapter.size} 行（点击${if (logCollapsed) "展开" else "折叠"}）"
            if (!logCollapsed) b.listLog.scrollToPosition(0)
        } else {
            ui.post { logLine(kind, msg) }
        }
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()

    @SuppressLint("MissingPermission")
    private fun safeName(d: BluetoothDevice?): String =
        try {
            d?.name ?: "未知设备"
        } catch (_: SecurityException) {
            "未知设备"
        }

    private fun showAbout() {
        MaterialAlertDialogBuilder(this)
            .setTitle("协议速查（@命令#* / 9600bps）")
            .setMessage(
                """
                |开闸        @K#*        ← 回 @KOK#*
                |关闸        @G#*        ← 回 @GOK#*
                |自动模式    @A#*        ← 回 @AOK#*
                |手动模式    @M#*        ← 回 @MOK#*
                |语音音量    @V050#*     ← 回 @VOK#*
                |车位检测器  @S1=1#*     ← 回 @SOK#*
                |状态查询    @Q#*        ← 回 @Q1205 011111#*
                |停车记录    （车库主动推送）
                |            @P粤A88888|201503|02#*
                |
                |状态位说明
                |  @Q 后第 1 位：1=自动 0=手动
                |  第 2 位：0=关闸 1=等待 2=开闸
                |  第 3~4 位：剩余车位数
                |  第 5~10 位：6 路检测器开关
                |
                |硬件：HC-05（经典蓝牙 SPP）
                |配对密码一般 123456，从机 MAC 见模块标签
                """.trimMargin()
            )
            .setPositiveButton("知道了", null)
            .show()
    }

    private companion object {
        const val PREFS = "garage_ble"
        const val KEY_LAST_MAC = "last_mac"
        const val KEY_RECORDS = "records"
        const val POLL_INTERVAL_MS = 2000L
        const val MAX_RECORDS = 100
    }
}
