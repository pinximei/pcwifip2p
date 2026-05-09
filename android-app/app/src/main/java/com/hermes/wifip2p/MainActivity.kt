package com.hermes.wifip2p

import android.Manifest
import android.content.pm.PackageManager
import android.net.wifi.p2p.WifiP2pDevice
import android.net.wifi.p2p.WifiP2pInfo
import android.os.Build
import android.os.Bundle
import android.view.inputmethod.EditorInfo
import android.widget.ArrayAdapter
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import com.hermes.wifip2p.databinding.ActivityMainBinding
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity(), WifiP2pClient.Listener {

    private lateinit var binding: ActivityMainBinding
    private lateinit var p2p: WifiP2pClient
    private var tcp: TcpClient? = null
    private var tcpHost: String? = null
    private var peers = listOf<WifiP2pDevice>()
    private lateinit var peerAdapter: ArrayAdapter<String>
    private val timeFmt = SimpleDateFormat("HH:mm:ss", Locale.US)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        peerAdapter = ArrayAdapter(this, android.R.layout.simple_list_item_1, mutableListOf())
        binding.peerList.adapter = peerAdapter

        // Make logView Editable so appendLog can do O(1) tail trim via Editable.delete()
        binding.logView.setText("", android.widget.TextView.BufferType.EDITABLE)

        p2p = WifiP2pClient(this)

        binding.discoverBtn.setOnClickListener { ensurePermissionsThen { p2p.discover() } }

        binding.manualTcpBtn.setOnClickListener {
            tcp?.close()
            tcpHost = "192.168.137.1"
            tcp = TcpClient("192.168.137.1", TCP_PORT) { line -> appendLog("RX", line) }
            tcp?.connect()
            setStatus("manual TCP -> 192.168.137.1:$TCP_PORT")
            binding.sendBtn.isEnabled = true
            binding.disconnectBtn.isEnabled = true
        }

        binding.peerList.setOnItemClickListener { _, _, pos, _ ->
            peers.getOrNull(pos)?.let { p2p.connect(it) }
        }

        val doSend: () -> Unit = doSend@{
            val text = binding.sendInput.text?.toString()?.trim().orEmpty()
            if (text.isEmpty()) {
                appendLog("INFO", "(empty input, nothing sent)")
                return@doSend
            }
            tcp?.sendLine(text)
            appendLog("TX", text)
            binding.sendInput.setText("")
        }
        binding.sendBtn.setOnClickListener { doSend() }
        binding.sendInput.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_SEND) {
                doSend(); true
            } else false
        }

        binding.disconnectBtn.setOnClickListener {
            tcp?.close()
            tcp = null
            tcpHost = null
            p2p.disconnect()
            setStatus("disconnected")
            binding.sendBtn.isEnabled = false
            binding.disconnectBtn.isEnabled = false
        }

        ensurePermissionsThen {
            p2p.init(this)
            setStatus("ready — tap Discover")
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        tcp?.close()
        p2p.release()
    }

    override fun onLog(msg: String) = appendLog("INFO", msg)

    override fun onPeersChanged(newPeers: List<WifiP2pDevice>) {
        peers = newPeers
        peerAdapter.clear()
        peerAdapter.addAll(newPeers.map { "${it.deviceName.ifBlank { "(no name)" }} — ${it.deviceAddress}" })
        peerAdapter.notifyDataSetChanged()
        appendLog("INFO", "peers: ${newPeers.size}")
    }

    override fun onConnected(info: WifiP2pInfo) {
        val goIp = info.groupOwnerAddress?.hostAddress ?: return
        appendLog("INFO", "group formed, GO=$goIp, isOwner=${info.isGroupOwner}")
        setStatus("connected to $goIp")
        if (info.isGroupOwner) {
            appendLog("WARN", "this device became GO (Windows intent=14 should win)")
            return
        }
        if (tcp != null && tcpHost == goIp) {
            appendLog("INFO", "tcp already connected to $goIp, keep existing socket")
            return
        }
        tcp?.close()
        tcpHost = goIp
        tcp = TcpClient(goIp, TCP_PORT) { line -> appendLog("RX", line) }
        tcp?.connect()
        binding.sendBtn.isEnabled = true
        binding.disconnectBtn.isEnabled = true
    }

    override fun onDisconnected() {
        appendLog("INFO", "p2p disconnected")
        tcp?.close()
        tcp = null
        tcpHost = null
        binding.sendBtn.isEnabled = false
        binding.disconnectBtn.isEnabled = false
        setStatus("disconnected")
    }

    private fun setStatus(s: String) {
        binding.statusView.text = "status: $s"
    }

    private var logLineCount = 0

    private fun appendLog(tag: String, line: String) {
        val ts = timeFmt.format(Date())
        val arrow = when (tag) {
            "RX" -> "<<"
            "TX" -> ">>"
            "WARN" -> "!!"
            else -> "  "
        }
        val newLine = "[$ts] $arrow $line\n"
        binding.logView.post {
            // Incremental append — TextView keeps its existing CharSequence,
            // we only push the new tail. This is O(1) instead of the previous
            // O(N) read-string + split + join + setText that caused the
            // multi-second lag the user reported.
            binding.logView.append(newLine)
            logLineCount++
            if (logLineCount > LOG_MAX_LINES) {
                val text = binding.logView.text
                val drop = (logLineCount - LOG_MAX_LINES_TARGET).coerceAtLeast(0)
                var cut = 0
                var seen = 0
                for (i in 0 until text.length) {
                    if (text[i] == '\n') {
                        seen++
                        if (seen == drop) { cut = i + 1; break }
                    }
                }
                if (cut > 0) {
                    (text as? android.text.Editable)?.delete(0, cut)
                        ?: run { binding.logView.text = text.subSequence(cut, text.length) }
                    logLineCount -= drop
                }
            }
            // Smooth-scroll only if the user is already near the bottom — avoids
            // fighting them when they scroll up to inspect history.
            val sv = binding.logScroll
            val nearBottom = sv.getChildAt(0)?.let {
                it.bottom - (sv.height + sv.scrollY) < 120
            } ?: true
            if (nearBottom) sv.post { sv.fullScroll(android.view.View.FOCUS_DOWN) }
        }
    }

    private fun ensurePermissionsThen(action: () -> Unit) {
        val needed = mutableListOf<String>()
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION)
            != PackageManager.PERMISSION_GRANTED) {
            needed += Manifest.permission.ACCESS_FINE_LOCATION
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ActivityCompat.checkSelfPermission(this, Manifest.permission.NEARBY_WIFI_DEVICES)
                != PackageManager.PERMISSION_GRANTED) {
                needed += Manifest.permission.NEARBY_WIFI_DEVICES
            }
        }
        if (needed.isEmpty()) {
            action()
        } else {
            ActivityCompat.requestPermissions(this, needed.toTypedArray(), REQ_PERMS)
            Toast.makeText(this, "Granted permissions, tap again", Toast.LENGTH_SHORT).show()
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_PERMS && grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
            p2p.init(this)
            setStatus("ready — tap Discover")
        }
    }

    companion object {
        private const val REQ_PERMS = 1001
        private const val TCP_PORT = 8888
        private const val LOG_MAX_LINES = 200
        private const val LOG_MAX_LINES_TARGET = 150
    }
}
