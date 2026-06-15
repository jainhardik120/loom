package com.jainhardik120.loom.stream

import java.io.ByteArrayOutputStream
import kotlin.math.max

class AnnexBParser {
    private var buffer = ByteArray(1024 * 1024)
    private var length = 0
    private val pendingAccessUnit = ByteArrayOutputStream(256 * 1024)

    fun append(bytes: ByteArray, count: Int) {
        ensureCapacity(length + count)
        System.arraycopy(bytes, 0, buffer, length, count)
        length += count
    }

    fun nextAccessUnit(): AccessUnit? {
        while (true) {
            val nal = nextNal() ?: return null
            val type = nalType(nal)
            val startsNewAccessUnit = type == NAL_AUD && pendingAccessUnit.size() > 0
            if (startsNewAccessUnit) {
                val accessUnit = pendingAccessUnit.toByteArray()
                pendingAccessUnit.reset()
                pendingAccessUnit.write(nal)
                return AccessUnit(
                    data = accessUnit,
                    isKeyFrame = containsIdr(accessUnit),
                    nalCount = countNals(accessUnit)
                )
            }
            pendingAccessUnit.write(nal)
        }
    }

    fun clear() {
        length = 0
        pendingAccessUnit.reset()
    }

    private fun nextNal(): ByteArray? {
        val first = findStartCode(0)
        if (first < 0) {
            if (length > MAX_BUFFER_BYTES) {
                clear()
            }
            return null
        }
        if (first > 0) {
            discard(first)
        }

        val second = findStartCode(startCodeLength(0))
        if (second < 0) {
            if (length > MAX_BUFFER_BYTES) {
                clear()
            }
            return null
        }

        val nal = buffer.copyOfRange(0, second)
        discard(second)
        return nal
    }

    private fun discard(count: Int) {
        if (count <= 0) return
        val remaining = length - count
        if (remaining > 0) {
            System.arraycopy(buffer, count, buffer, 0, remaining)
        }
        length = remaining
    }

    private fun ensureCapacity(required: Int) {
        if (required <= buffer.size) return
        var newSize = max(buffer.size * 2, required)
        while (newSize < required) {
            newSize *= 2
        }
        buffer = buffer.copyOf(newSize)
    }

    private fun findStartCode(from: Int): Int {
        var i = from
        while (i + 3 < length) {
            if (buffer[i] == 0.toByte() && buffer[i + 1] == 0.toByte()) {
                if (buffer[i + 2] == 1.toByte()) return i
                if (i + 4 < length && buffer[i + 2] == 0.toByte() && buffer[i + 3] == 1.toByte()) {
                    return i
                }
            }
            i++
        }
        return -1
    }

    private fun startCodeLength(offset: Int): Int {
        return if (length > offset + 3 && buffer[offset + 2] == 1.toByte()) 3 else 4
    }

    data class AccessUnit(
        val data: ByteArray,
        val isKeyFrame: Boolean,
        val nalCount: Int
    )

    companion object {
        private const val MAX_BUFFER_BYTES = 8 * 1024 * 1024
        private const val NAL_AUD = 9

        fun nalType(nal: ByteArray): Int {
            val offset = nalPayloadOffset(nal)
            if (offset >= nal.size) return -1
            return nal[offset].toInt() and 0x1f
        }

        fun nalPayloadOffset(nal: ByteArray): Int {
            return when {
                nal.size > 4 && nal[0] == 0.toByte() && nal[1] == 0.toByte() &&
                    nal[2] == 0.toByte() && nal[3] == 1.toByte() -> 4
                nal.size > 3 && nal[0] == 0.toByte() && nal[1] == 0.toByte() &&
                    nal[2] == 1.toByte() -> 3
                else -> 0
            }
        }

        private fun containsIdr(accessUnit: ByteArray): Boolean {
            var i = 0
            while (i + 4 < accessUnit.size) {
                if (accessUnit[i] == 0.toByte() && accessUnit[i + 1] == 0.toByte()) {
                    val offset = when {
                        accessUnit[i + 2] == 1.toByte() -> i + 3
                        i + 3 < accessUnit.size && accessUnit[i + 2] == 0.toByte() &&
                            accessUnit[i + 3] == 1.toByte() -> i + 4
                        else -> -1
                    }
                    if (offset >= 0 && offset < accessUnit.size &&
                        (accessUnit[offset].toInt() and 0x1f) == 5
                    ) {
                        return true
                    }
                }
                i++
            }
            return false
        }

        private fun countNals(accessUnit: ByteArray): Int {
            var count = 0
            var i = 0
            while (i + 4 < accessUnit.size) {
                if (accessUnit[i] == 0.toByte() && accessUnit[i + 1] == 0.toByte() &&
                    (accessUnit[i + 2] == 1.toByte() ||
                        (i + 3 < accessUnit.size && accessUnit[i + 2] == 0.toByte() &&
                            accessUnit[i + 3] == 1.toByte()))
                ) {
                    count++
                    i += 3
                } else {
                    i++
                }
            }
            return count
        }
    }
}
