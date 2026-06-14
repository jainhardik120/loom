package com.jainhardik120.loom.stream

class AnnexBParser {
    private val data = ArrayList<Byte>(1024 * 256)

    fun append(bytes: ByteArray, count: Int) {
        for (i in 0 until count) {
            data.add(bytes[i])
        }
    }

    fun nextNal(): ByteArray? {
        val first = findStartCode(0) ?: run {
            if (data.size > 4 * 1024 * 1024) data.clear()
            return null
        }
        if (first > 0) {
            data.subList(0, first).clear()
        }

        val second = findStartCode(startCodeLength(0)) ?: return null
        val nal = ByteArray(second)
        for (i in 0 until second) {
            nal[i] = data[i]
        }
        data.subList(0, second).clear()
        return nal
    }

    private fun findStartCode(from: Int): Int? {
        var i = from
        while (i + 3 < data.size) {
            if (data[i] == 0.toByte() && data[i + 1] == 0.toByte()) {
                if (data[i + 2] == 1.toByte()) return i
                if (i + 4 < data.size && data[i + 2] == 0.toByte() && data[i + 3] == 1.toByte()) {
                    return i
                }
            }
            i++
        }
        return null
    }

    private fun startCodeLength(offset: Int): Int {
        return if (data.size > offset + 3 && data[offset + 2] == 1.toByte()) 3 else 4
    }
}
