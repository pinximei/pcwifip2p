package com.hermes.wifip2p

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.PrintWriter
import java.net.InetSocketAddress
import java.net.Socket

/**
 * Stage 1 line-based client. Speaks the WIFIP2P-DEMO/1 text protocol:
 *   server -> "WIFIP2P-DEMO/1"
 *   client -> "HELLO <name>"   server -> "WELCOME <name>"
 *   client -> "PING"           server -> "PONG"
 *   client -> anything else    server -> "ECHO <UPPERCASED>"
 *
 * Stage 2 will swap [sendLine] for raw H.264 NAL writes once the link is
 * proven.
 */
class TcpClient(
    private val host: String,
    private val port: Int,
    private val onLog: (String) -> Unit,
) {
    private val scope = CoroutineScope(Dispatchers.IO + SupervisorJob())
    private var socket: Socket? = null
    private var writer: PrintWriter? = null
    private var readerJob: Job? = null

    fun connect() = scope.launch {
        try {
            onLog("tcp: connecting to $host:$port")
            val s = Socket()
            s.connect(InetSocketAddress(host, port), 5_000)
            s.tcpNoDelay = true
            socket = s
            writer = PrintWriter(s.getOutputStream(), true)
            onLog("tcp: connected (nodelay=${s.tcpNoDelay})")

            readerJob = scope.launch {
                try {
                    onLog("tcp: reader started")
                    val r = BufferedReader(InputStreamReader(s.getInputStream(), Charsets.UTF_8))
                    while (isActive) {
                        val line = r.readLine() ?: break
                        onLog("tcp recv: $line")
                    }
                    onLog("tcp: closed by peer")
                } catch (e: Exception) {
                    onLog("tcp: reader crashed — ${e.javaClass.simpleName}: ${e.message}")
                }
            }

            // greet so the GO logs show "[TCP][rx] HELLO ..." immediately
            sendLine("HELLO android-${android.os.Build.MODEL}")
        } catch (e: Exception) {
            onLog("tcp: connect failed — ${e.message}")
        }
    }

    fun sendLine(line: String) {
        scope.launch {
            withContext(Dispatchers.IO) {
                runCatching {
                    writer?.println(line)
                    writer?.flush()
                }
            }
        }
    }

    fun close() {
        readerJob?.cancel()
        runCatching { writer?.close() }
        runCatching { socket?.close() }
        socket = null
        writer = null
        scope.cancel()
    }
}
