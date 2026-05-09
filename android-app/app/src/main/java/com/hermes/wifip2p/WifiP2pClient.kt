package com.hermes.wifip2p

import android.Manifest
import android.annotation.SuppressLint
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.net.wifi.p2p.WifiP2pConfig
import android.net.wifi.p2p.WifiP2pDevice
import android.net.wifi.p2p.WifiP2pInfo
import android.net.wifi.p2p.WifiP2pManager
import android.os.Build
import android.os.Looper
import android.util.Log

/**
 * Thin wrapper around [WifiP2pManager]. Demo-only — no permission UI; assumes
 * caller already prompted for ACCESS_FINE_LOCATION (and NEARBY_WIFI_DEVICES on
 * Android 13+).
 */
class WifiP2pClient(private val ctx: Context) {

    interface Listener {
        fun onLog(msg: String)
        fun onPeersChanged(peers: List<WifiP2pDevice>)
        fun onConnected(info: WifiP2pInfo)
        fun onDisconnected()
    }

    private val manager: WifiP2pManager =
        ctx.getSystemService(Context.WIFI_P2P_SERVICE) as WifiP2pManager
    private var channel: WifiP2pManager.Channel? = null
    private var receiver: BroadcastReceiver? = null
    private var listener: Listener? = null

    fun init(listener: Listener) {
        this.listener = listener
        channel = manager.initialize(ctx, Looper.getMainLooper(), null)

        val filter = IntentFilter().apply {
            addAction(WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION)
            addAction(WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION)
            addAction(WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION)
            addAction(WifiP2pManager.WIFI_P2P_THIS_DEVICE_CHANGED_ACTION)
        }

        receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) {
                when (intent.action) {
                    WifiP2pManager.WIFI_P2P_STATE_CHANGED_ACTION -> {
                        val state = intent.getIntExtra(WifiP2pManager.EXTRA_WIFI_STATE, -1)
                        listener.onLog("p2p state=$state (2=enabled)")
                    }
                    WifiP2pManager.WIFI_P2P_PEERS_CHANGED_ACTION -> {
                        requestPeers()
                        // Defensive: some OEMs (e.g. Huawei EMUI) drop or
                        // mis-populate EXTRA_NETWORK_INFO on the connection
                        // broadcast. Polling connection info here means we
                        // always pick up "groupFormed" within one peer-update
                        // cycle, even when the dedicated broadcast lies.
                        requestConnectionInfo()
                    }
                    WifiP2pManager.WIFI_P2P_CONNECTION_CHANGED_ACTION -> {
                        // EXTRA_NETWORK_INFO is unreliable on Android 12+ /
                        // EMUI; just unconditionally re-query and let the
                        // info object decide.
                        requestConnectionInfo()
                    }
                }
            }
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            ctx.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            ctx.registerReceiver(receiver, filter)
        }
    }

    fun release() {
        receiver?.let { runCatching { ctx.unregisterReceiver(it) } }
        receiver = null
        channel = null
        listener = null
    }

    @SuppressLint("MissingPermission")
    fun discover() {
        val ch = channel ?: return
        manager.discoverPeers(ch, object : WifiP2pManager.ActionListener {
            override fun onSuccess() = log("discoverPeers ok")
            override fun onFailure(reason: Int) = log("discoverPeers fail=$reason")
        })
    }

    @SuppressLint("MissingPermission")
    fun connect(device: WifiP2pDevice) {
        val ch = channel ?: return
        val cfg = WifiP2pConfig().apply {
            deviceAddress = device.deviceAddress
            wps.setup = android.net.wifi.WpsInfo.PBC
            // groupOwnerIntent left at default (auto). Windows uses intent=14
            // so it should always become GO.
        }
        manager.connect(ch, cfg, object : WifiP2pManager.ActionListener {
            override fun onSuccess() = log("connect ok (waiting for GO)")
            override fun onFailure(reason: Int) = log("connect fail=$reason")
        })
    }

    fun disconnect() {
        val ch = channel ?: return
        manager.removeGroup(ch, object : WifiP2pManager.ActionListener {
            override fun onSuccess() = log("removeGroup ok")
            override fun onFailure(reason: Int) = log("removeGroup fail=$reason")
        })
    }

    @SuppressLint("MissingPermission")
    private fun requestPeers() {
        val ch = channel ?: return
        manager.requestPeers(ch) { peerList ->
            listener?.onPeersChanged(peerList.deviceList.toList())
        }
    }

    private var lastReportedGroupFormed = false

    private fun requestConnectionInfo() {
        val ch = channel ?: return
        manager.requestConnectionInfo(ch) { info ->
            val formed = info?.groupFormed == true
            if (formed && !lastReportedGroupFormed) {
                lastReportedGroupFormed = true
                listener?.onConnected(info!!)
            } else if (!formed && lastReportedGroupFormed) {
                lastReportedGroupFormed = false
                listener?.onDisconnected()
            }
        }
    }

    private fun log(s: String) {
        Log.d("wifip2p", s)
        listener?.onLog(s)
    }
}
