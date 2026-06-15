package com.jainhardik120.loom.stream

import java.io.Closeable
import java.io.InputStream
import java.util.concurrent.atomic.AtomicBoolean

interface StreamInputSource : Closeable {
    val name: String
    fun open(running: AtomicBoolean): InputStream?
}
