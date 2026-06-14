package com.jainhardik120.loom.stream

data class StreamStats(
    val status: String = "Waiting for Loom stream",
    val fps: Double = 0.0,
    val bitrateKbps: Double = 0.0,
    val width: Int = 0,
    val height: Int = 0,
    val queuedSamples: Long = 0,
    val renderedFrames: Long = 0,
    val droppedNals: Long = 0,
    val port: Int = 27183
)
