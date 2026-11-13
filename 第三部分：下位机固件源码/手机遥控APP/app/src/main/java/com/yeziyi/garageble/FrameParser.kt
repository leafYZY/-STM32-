package com.yeziyi.garageble

import java.io.ByteArrayOutputStream

/**
 * '@载荷#*' 帧解析状态机 —— 逻辑与 STM32 端 BLE_Serial.c / Serial.c 的接收中断保持一致。
 *
 * 由于串口是字节流，一次 read() 可能读到半帧或多帧，所以必须用状态机缓存。
 * 用 ByteArrayOutputStream 而不是 StringBuilder，是为了保证车牌里万一出现非 ASCII
 * 字符（GBK/UTF-8 多字节）时不会把字节拆坏。
 */
class FrameParser(private val onFrame: (String) -> Unit) {

    private val buffer = ByteArrayOutputStream(64)
    private var state = 0

    fun feed(data: ByteArray, length: Int) {
        for (i in 0 until length) {
            val b = data[i].toInt() and 0xFF
            when (state) {
                0 -> if (b == CHAR_AT) {          // 等待帧头 '@'
                    buffer.reset()
                    state = 1
                }

                1 -> if (b == CHAR_HASH) {        // 载荷结束符 '#'
                    state = 2
                } else {
                    buffer.write(b)
                }

                2 -> if (b == CHAR_STAR) {        // 帧尾 '*'
                    onFrame(String(buffer.toByteArray(), Charsets.UTF_8))
                    state = 0
                } else {
                    // 意外字符：当作载荷继续累积，避免整帧被丢掉
                    state = 1
                    buffer.write(b)
                }
            }
        }
    }

    fun reset() {
        state = 0
        buffer.reset()
    }

    private companion object {
        const val CHAR_AT = 0x40    // '@'
        const val CHAR_HASH = 0x23  // '#'
        const val CHAR_STAR = 0x2A  // '*'
    }
}
