package com.jainhardik120.loom.stream

import java.io.InputStream
import java.util.concurrent.atomic.AtomicBoolean

class CompositeStreamSource(
    private val sources: List<StreamInputSource>
) : StreamInputSource {
    override val name = sources.joinToString(" or ") { it.name }

    override fun open(running: AtomicBoolean): InputStream? {
        while (running.get()) {
            for (source in sources) {
                val input = try {
                    source.open(running)
                } catch (_: Exception) {
                    null
                }
                if (input != null) {
                    return input
                }
            }
            Thread.sleep(500)
        }
        return null
    }

    override fun close() {
        sources.forEach { it.close() }
    }
}
