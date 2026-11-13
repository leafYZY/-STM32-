package com.yeziyi.garageble

import java.util.Locale
import java.util.UUID

/**
 * 车库蓝牙协议（与 STM32 固件严格对齐）
 *
 * 帧格式：'@' + 载荷 + '#' + '*'
 *   —— 车库端 BLE_Serial.c 的状态机为：遇 '@' 开始，遇 '#' 进入结尾态，遇 '*' 判定收完。
 *   —— 遥控器端 Serial.c 的状态机为：遇 '@' 开始，遇 '*' 判定收完。
 *   两边都兼容 '@载荷#*'，所以 APP 发送时【不要】附加 \r\n，与遥控器保持一致最稳妥。
 *
 * 波特率 9600 / 8-N-1，蓝牙为 HC-05（经典蓝牙 SPP），因此必须用 RFCOMM 而不是 BLE。
 */
object Protocol {

    /** 经典蓝牙串口（SPP）标准 UUID —— HC-05 / HC-06 都用这个 */
    val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")

    /** 开闸（对应遥控器 SW1 / 主菜单“开闸”） */
    fun openGate(): String = "@K#*"

    /** 关闸（对应遥控器 SW2 / 主菜单“关闸”） */
    fun closeGate(): String = "@G#*"

    /** 切换到自动模式（扫码模式） */
    fun modeAuto(): String = "@A#*"

    /** 切换到手动模式（遥控模式） */
    fun modeManual(): String = "@M#*"

    /**
     * 设置语音播报音量
     * @param volume 0~100，自动补足 3 位，例如 50 -> "@V050#*"
     *               车库端用 atoi("050") 解析，与遥控器 @V%03d#* 完全一致
     */
    fun setVolume(volume: Int): String {
        val v = volume.coerceIn(0, 100)
        return String.format(Locale.US, "@V%03d#*", v)
    }

    /**
     * 设置第 N 路车位检测器开关
     * @param index 1~6（1~3 = L1 层 1~3 号，4~6 = L2 层 1~3 号）
     * 车库端解析：BLE_RxPacket[1] - '0' 取编号，[3] - '0' 取开关值
     */
    fun setSensor(index: Int, enable: Boolean): String =
        "@S$index=${if (enable) 1 else 0}#*"

    /**
     * 状态查询（需要在车库固件里打补丁后才会回复，见 README「固件改动」）
     * 回复：@Q<模式><闸门><空位2位><传感器6位>#*
     */
    fun queryStatus(): String = "@Q#*"

    /** 把一条指令翻译成人话，用于日志显示 */
    fun describeCommand(cmd: String): String = when {
        cmd == openGate() -> "开闸"
        cmd == closeGate() -> "关闸"
        cmd == modeAuto() -> "切换自动模式"
        cmd == modeManual() -> "切换手动模式"
        cmd == queryStatus() -> "查询车库状态"
        cmd.startsWith("@V") -> "设置语音音量 = ${cmd.substring(2, 5).toIntOrNull() ?: "?"}"
        cmd.startsWith("@S") -> "车位检测器 ${cmd[2]} -> ${if (cmd.getOrNull(4) == '1') "启用" else "禁用"}"
        else -> cmd
    }

    /**
     * 解析车库回复的「载荷」（已去掉 '@' 和 '#*'）
     */
    fun parseReply(payload: String): GarageReply = when {
        payload == "KOK" -> GarageReply.Ack(GateAction.OPEN)
        payload == "GOK" -> GarageReply.Ack(GateAction.CLOSE)
        payload == "AOK" -> GarageReply.Ack(GateAction.MODE_AUTO)
        payload == "MOK" -> GarageReply.Ack(GateAction.MODE_MANUAL)
        payload == "VOK" -> GarageReply.Ack(GateAction.VOLUME)
        payload == "SOK" -> GarageReply.Ack(GateAction.SENSOR)
        payload.length >= 5 && payload[0] == 'Q' -> {
            val free = payload.substring(3, 5).trim().toIntOrNull() ?: 0
            val mask = if (payload.length >= 11) payload.substring(5, 11) else "111111"
            GarageReply.Status(
                autoMode = payload[1] == '1',
                gate = payload[2] - '0',
                freeSpaces = free,
                sensorMask = mask
            )
        }
        payload.length >= 3 && payload[0] == 'P' && payload.contains('|') -> {
            val parts = payload.substring(1).split('|')
            if (parts.size >= 3) {
                GarageReply.Parking(
                    plate = parts[0],
                    hhmmss = parts[1],
                    space = parts[2].toIntOrNull() ?: 0
                )
            } else GarageReply.Unknown(payload)
        }
        else -> GarageReply.Unknown(payload)
    }

    /** 闸门状态：0=空闲(红灯) 1=等待(黄灯) 2=开闸(绿灯) */
    fun gateText(gate: Int): String = when (gate) {
        0 -> "已关闭"
        1 -> "等待通行"
        2 -> "已开启"
        else -> "未知"
    }

    /** 车位号 1~6 -> 楼层显示 */
    fun spaceText(space: Int): String = when (space) {
        in 1..3 -> "L1-${space}"
        in 4..6 -> "L2-${space - 3}"
        else -> "--"
    }
}

enum class GateAction { OPEN, CLOSE, MODE_AUTO, MODE_MANUAL, VOLUME, SENSOR }

/** 车库回复的类型化表示 */
sealed interface GarageReply {
    data class Ack(val action: GateAction) : GarageReply

    data class Status(
        val autoMode: Boolean,
        val gate: Int,
        val freeSpaces: Int,
        val sensorMask: String
    ) : GarageReply

    data class Parking(val plate: String, val hhmmss: String, val space: Int) : GarageReply

    data class Unknown(val raw: String) : GarageReply
}
