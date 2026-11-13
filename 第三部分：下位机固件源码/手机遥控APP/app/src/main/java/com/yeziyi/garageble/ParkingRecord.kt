package com.yeziyi.garageble

/** 一条停车记录（由车库经蓝牙主动推送 @P<车牌>|<HHMMSS>|<车位>#*） */
data class ParkingRecord(
    val plate: String,
    val hhmmss: String,
    val space: Int,
    val receivedAt: Long = System.currentTimeMillis()
) {
    /** 车位显示：1~3 -> L1-x，4~6 -> L2-x */
    val spaceText: String get() = Protocol.spaceText(space)

    /** 220130 -> 22:01:30 */
    val timeText: String
        get() = if (hhmmss.length == 6) {
            "${hhmmss.substring(0, 2)}:${hhmmss.substring(2, 4)}:${hhmmss.substring(4, 6)}"
        } else hhmmss

    /** 用于本地持久化的编码（字段内不含 '|'） */
    fun encode(): String = "$plate|$hhmmss|$space"

    /** 去重键：同一车牌 + 同一时间 + 同一车位视为同一条 */
    fun dedupKey(): String = "$plate|$hhmmss|$space"

    companion object {
        fun decode(text: String): ParkingRecord? {
            val p = text.split('|')
            if (p.size != 3) return null
            return ParkingRecord(p[0], p[1], p[2].toIntOrNull() ?: return null)
        }
    }
}
