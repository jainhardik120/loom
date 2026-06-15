package com.jainhardik120.loom.stream

import android.media.MediaCodec
import android.media.MediaFormat
import android.os.SystemClock
import android.util.Log
import android.view.Surface
import java.io.BufferedInputStream
import java.io.InputStream
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

class H264StreamDecoder(
    private val port: Int,
    private val source: StreamInputSource,
    private val onStats: (StreamStats) -> Unit
) {
    private val tag = "LoomDecoder"
    private val statsTag = "LoomStats"
    private val maxDecodeBacklog = 8L
    private val presentationIntervalUs = 16_666L
    private val running = AtomicBoolean(false)
    private var worker: Thread? = null
    private var stats = StreamStats(port = port)

    fun start(surface: Surface) {
        if (!running.compareAndSet(false, true)) {
            Log.i(tag, "decoder already running")
            return
        }

        Log.i(tag, "starting decoder from ${source.name}")
        worker = thread(name = "loom-h264-decoder") {
            runDecoder(surface)
        }
    }

    fun stop() {
        running.set(false)
        Log.i(tag, "stopping decoder")
        source.close()
        worker?.interrupt()
        worker = null
    }

    private fun runDecoder(surface: Surface) {
        var codec: MediaCodec? = null
        try {
            while (running.get()) {
                publishStats(stats.copy(status = "Waiting on ${source.name}"))
                val input = source.open(running) ?: continue
                publishStats(stats.copy(status = "Stream connected (${source.name})"))
                Log.i(tag, "stream connected from ${source.name}")

                codec = MediaCodec.createDecoderByType("video/avc")
                val format = MediaFormat.createVideoFormat("video/avc", 1920, 1080)
                publishStats(stats.copy(width = 1920, height = 1080))
                codec.configure(format, surface, null, 0)
                codec.start()

                decodeInput(input, codec)

                codec.stop()
                codec.release()
                codec = null
                source.close()
                publishStats(
                    stats.copy(
                        status = "Stream disconnected",
                        fps = 0.0,
                        bitrateKbps = 0.0
                    )
                )
                Log.i(tag, "stream disconnected")
            }
        } catch (error: Exception) {
            if (running.get()) {
                publishStats(stats.copy(status = "Decoder error: ${error.message}"))
                Log.e(tag, "decoder error", error)
            }
        } finally {
            try {
                codec?.stop()
            } catch (_: Exception) {
            }
            try {
                codec?.release()
            } catch (_: Exception) {
            }
            running.set(false)
            source.close()
        }
    }

    private fun decodeInput(inputStream: InputStream, codec: MediaCodec) {
        val parser = AnnexBParser()
        val input = BufferedInputStream(inputStream, 1024 * 256)
        val bufferInfo = MediaCodec.BufferInfo()
        val readBuffer = ByteArray(64 * 1024)
        var presentationTimeUs = 0L
        var bytesInWindow = 0L
        var renderedInWindow = 0L
        var smoothedFps = stats.fps
        var queuedSamples = stats.queuedSamples
        var renderedFrames = stats.renderedFrames
        var droppedAccessUnits = stats.droppedNals
        var windowStartedAt = SystemClock.elapsedRealtime()

        while (running.get()) {
            val read = input.read(readBuffer)
            if (read < 0) break
            if (read == 0) continue

            bytesInWindow += read.toLong()
            parser.append(readBuffer, read)
            while (true) {
                val drainResult = drainOutput(codec, bufferInfo)
                renderedInWindow += drainResult.renderedFrames
                renderedFrames += drainResult.renderedFrames
                if (drainResult.width > 0 && drainResult.height > 0) {
                    publishStats(stats.copy(width = drainResult.width, height = drainResult.height))
                }

                val accessUnit = parser.nextAccessUnit() ?: break
                val backlog = queuedSamples - renderedFrames
                if (backlog > maxDecodeBacklog && !accessUnit.isKeyFrame) {
                    droppedAccessUnits += 1
                    continue
                }

                val inputTimeoutUs = if (accessUnit.isKeyFrame) 5_000L else 0L
                val inputIndex = codec.dequeueInputBuffer(inputTimeoutUs)
                if (inputIndex < 0) {
                    droppedAccessUnits += 1
                    continue
                }

                val inputBuffer = codec.getInputBuffer(inputIndex) ?: continue
                inputBuffer.clear()
                if (accessUnit.data.size <= inputBuffer.capacity()) {
                    inputBuffer.put(accessUnit.data)
                    codec.queueInputBuffer(
                        inputIndex,
                        0,
                        accessUnit.data.size,
                        presentationTimeUs,
                        sampleFlags(accessUnit)
                    )
                    presentationTimeUs += presentationIntervalUs
                    queuedSamples += 1
                } else {
                    droppedAccessUnits += 1
                }
            }
            val drainResult = drainOutput(codec, bufferInfo)
            renderedInWindow += drainResult.renderedFrames
            renderedFrames += drainResult.renderedFrames
            if (drainResult.width > 0 && drainResult.height > 0) {
                publishStats(stats.copy(width = drainResult.width, height = drainResult.height))
            }

            val now = SystemClock.elapsedRealtime()
            val elapsedMs = now - windowStartedAt
            if (elapsedMs >= 1_000) {
                val seconds = elapsedMs / 1_000.0
                val measuredFps = renderedInWindow / seconds
                smoothedFps = if (smoothedFps <= 0.0) {
                    measuredFps
                } else {
                    (smoothedFps * 0.75) + (measuredFps * 0.25)
                }
                publishStats(
                    stats.copy(
                        status = "Stream connected",
                        fps = smoothedFps,
                        bitrateKbps = (bytesInWindow * 8.0) / elapsedMs,
                        queuedSamples = queuedSamples,
                        renderedFrames = renderedFrames,
                        droppedNals = droppedAccessUnits
                    )
                )
                Log.i(
                    statsTag,
                    "fps=%.2f bitrate_kbps=%.0f resolution=%dx%d queued=%d rendered=%d backlog=%d dropped_access_units=%d source=\"%s\""
                        .format(
                            smoothedFps,
                            (bytesInWindow * 8.0) / elapsedMs,
                            stats.width,
                            stats.height,
                            queuedSamples,
                            renderedFrames,
                            queuedSamples - renderedFrames,
                            droppedAccessUnits,
                            source.name
                        )
                )
                bytesInWindow = 0L
                renderedInWindow = 0L
                windowStartedAt = now
            }
        }
    }

    private fun drainOutput(
        codec: MediaCodec,
        bufferInfo: MediaCodec.BufferInfo
    ): DrainResult {
        var renderedFrames = 0L
        var width = 0
        var height = 0
        while (true) {
            val outputIndex = codec.dequeueOutputBuffer(bufferInfo, 0)
            if (outputIndex >= 0) {
                val render = bufferInfo.size > 0
                codec.releaseOutputBuffer(outputIndex, render)
                if (render) {
                    renderedFrames += 1
                }
            } else if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                val format = codec.outputFormat
                width = getFormatInt(format, MediaFormat.KEY_WIDTH)
                height = getFormatInt(format, MediaFormat.KEY_HEIGHT)
                Log.i(tag, "decoder output format changed: $format")
            } else if (outputIndex == MediaCodec.INFO_TRY_AGAIN_LATER ||
                outputIndex == MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED
            ) {
                return DrainResult(renderedFrames, width, height)
            } else {
                return DrainResult(renderedFrames, width, height)
            }
        }
    }

    private fun getFormatInt(format: MediaFormat, key: String): Int {
        return if (format.containsKey(key)) format.getInteger(key) else 0
    }

    private fun publishStats(next: StreamStats) {
        stats = next
        onStats(next)
    }

    private data class DrainResult(
        val renderedFrames: Long,
        val width: Int,
        val height: Int
    )

    private fun sampleFlags(accessUnit: AnnexBParser.AccessUnit): Int {
        return if (accessUnit.isKeyFrame) {
            MediaCodec.BUFFER_FLAG_KEY_FRAME
        } else {
            0
        }
    }
}
