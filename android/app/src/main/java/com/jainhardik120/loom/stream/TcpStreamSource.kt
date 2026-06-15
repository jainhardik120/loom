package com.jainhardik120.loom.stream

import android.util.Log
import java.io.BufferedInputStream
import java.io.InputStream
import java.net.ServerSocket
import java.net.Socket
import java.net.SocketTimeoutException
import java.util.concurrent.atomic.AtomicBoolean

class TcpStreamSource(private val port: Int) : StreamInputSource {
    override val name = "TCP 127.0.0.1:$port"
    private val tag = "LoomTcpSource"
    private var serverSocket: ServerSocket? = null
    private var clientSocket: Socket? = null

    override fun open(running: AtomicBoolean): InputStream? {
        serverSocket = ServerSocket(port).apply {
            soTimeout = 500
        }
        Log.i(tag, "listening on port $port")
        while (running.get()) {
            val socket = try {
                serverSocket?.accept()
            } catch (_: SocketTimeoutException) {
                close()
                return null
            } ?: return null
            socket.tcpNoDelay = true
            clientSocket = socket
            Log.i(tag, "stream connected from ${socket.remoteSocketAddress}")
            return BufferedInputStream(socket.getInputStream(), 1024 * 256)
        }
        return null
    }

    override fun close() {
        try {
            clientSocket?.close()
        } catch (_: Exception) {
        }
        try {
            serverSocket?.close()
        } catch (_: Exception) {
        }
        clientSocket = null
        serverSocket = null
    }
}
