package com.jainhardik120.loom.stream

import android.media.MediaCodec
import android.media.MediaFormat
import android.util.Log
import android.view.Surface
import java.io.BufferedInputStream
import java.net.ServerSocket
import java.net.Socket
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

class H264StreamDecoder(
    private val port: Int,
    private val onStatus: (String) -> Unit
) {
    private val tag = "LoomDecoder"
    private val running = AtomicBoolean(false)
    private var worker: Thread? = null
    private var serverSocket: ServerSocket? = null
    private var clientSocket: Socket? = null

    fun start(surface: Surface) {
        if (!running.compareAndSet(false, true)) {
            Log.i(tag, "decoder already running")
            return
        }

        Log.i(tag, "starting decoder server on port $port")
        worker = thread(name = "loom-h264-decoder") {
            runDecoder(surface)
        }
    }

    fun stop() {
        running.set(false)
        Log.i(tag, "stopping decoder server")
        try {
            clientSocket?.close()
        } catch (_: Exception) {
        }
        try {
            serverSocket?.close()
        } catch (_: Exception) {
        }
        worker?.interrupt()
        worker = null
    }

    private fun runDecoder(surface: Surface) {
        var codec: MediaCodec? = null
        try {
            onStatus("Listening on 127.0.0.1:$port")
            Log.i(tag, "listening on port $port")
            serverSocket = ServerSocket(port)
            while (running.get()) {
                clientSocket = serverSocket?.accept()
                val socket = clientSocket ?: continue
                socket.tcpNoDelay = true
                onStatus("Stream connected")
                Log.i(tag, "stream connected from ${socket.remoteSocketAddress}")

                codec = MediaCodec.createDecoderByType("video/avc")
                val format = MediaFormat.createVideoFormat("video/avc", 1920, 1080)
                codec.configure(format, surface, null, 0)
                codec.start()

                decodeSocket(socket, codec)

                codec.stop()
                codec.release()
                codec = null
                onStatus("Stream disconnected")
                Log.i(tag, "stream disconnected")
            }
        } catch (error: Exception) {
            if (running.get()) {
                onStatus("Decoder error: ${error.message}")
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
        }
    }

    private fun decodeSocket(socket: Socket, codec: MediaCodec) {
        val parser = AnnexBParser()
        val input = BufferedInputStream(socket.getInputStream(), 1024 * 256)
        val bufferInfo = MediaCodec.BufferInfo()
        val readBuffer = ByteArray(64 * 1024)
        var presentationTimeUs = 0L

        while (running.get() && !socket.isClosed) {
            val read = input.read(readBuffer)
            if (read < 0) break
            if (read == 0) continue

            parser.append(readBuffer, read)
            while (true) {
                val nal = parser.nextNal() ?: break
                val inputIndex = codec.dequeueInputBuffer(10_000)
                if (inputIndex >= 0) {
                    val inputBuffer = codec.getInputBuffer(inputIndex) ?: continue
                    inputBuffer.clear()
                    if (nal.size <= inputBuffer.capacity()) {
                        inputBuffer.put(nal)
                        codec.queueInputBuffer(
                            inputIndex,
                            0,
                            nal.size,
                            presentationTimeUs,
                            nalFlags(nal)
                        )
                        presentationTimeUs += 33_333
                    }
                }

                drainOutput(codec, bufferInfo)
            }
            drainOutput(codec, bufferInfo)
        }
    }

    private fun drainOutput(codec: MediaCodec, bufferInfo: MediaCodec.BufferInfo) {
        while (true) {
            val outputIndex = codec.dequeueOutputBuffer(bufferInfo, 0)
            if (outputIndex >= 0) {
                codec.releaseOutputBuffer(outputIndex, bufferInfo.size > 0)
            } else if (
                outputIndex == MediaCodec.INFO_TRY_AGAIN_LATER ||
                outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED ||
                outputIndex == MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED
            ) {
                return
            } else {
                return
            }
        }
    }

    private fun nalFlags(nal: ByteArray): Int {
        val offset = when {
            nal.size > 4 && nal[0] == 0.toByte() && nal[1] == 0.toByte() &&
                nal[2] == 0.toByte() && nal[3] == 1.toByte() -> 4
            nal.size > 3 && nal[0] == 0.toByte() && nal[1] == 0.toByte() &&
                nal[2] == 1.toByte() -> 3
            else -> 0
        }
        if (offset >= nal.size) return 0
        return when (nal[offset].toInt() and 0x1f) {
            5 -> MediaCodec.BUFFER_FLAG_KEY_FRAME
            7, 8 -> MediaCodec.BUFFER_FLAG_CODEC_CONFIG
            else -> 0
        }
    }
}
