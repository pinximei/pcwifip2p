# wifip2p — Demo Status

最后更新：2026-05-09 15:15

## Stage 1：Wi-Fi Direct 配对 + TCP 数据通路 ✅ 已验证

### 实测产出（2026-05-09 真机跑通）

测试环境：
- **PC**：Windows 10 LTSC 2019，Intel Wi-Fi 6 AX201（驱动报告 `Hosted network supported : No`）
- **手机**：HUAWEI nova 12s（FOA-LX9，Android 12 API 31，EMUI）
- **关系**：USB 用于 adb 部署/调试，**实际数据走真 Wi-Fi Direct P2P 协议**，无 USB tether

实测日志（关键事件）：
```
[Windows GO]
[15:13:35.589] I [GO] publisher status: 1
[15:13:35.612] I [TCP] listening on 0.0.0.0:8888, waiting for client...
[15:13:38.620] I [GO] (p2p) peer connecting: HUAWEI nova 12s
[15:13:38.666] I [GO] (p2p) peer joined, ip=192.168.137.247

[早一轮跑通的完整 echo]
[15:10:52.857] I [TCP] client connected from 192.168.137.247:44604
[15:10:52.958] I [TCP][rx] HELLO android-FOA-LX9
```

手机端实测：
```
$ adb shell ip addr show p2p0
25: p2p0    inet 192.168.137.247/24 brd 192.168.137.255

$ adb shell ping -c 3 -W 2 192.168.137.1
3 packets transmitted, 3 received, 0% packet loss
rtt min/avg/max/mdev = 167/442/670/208 ms
```

### 协议层证据已凑齐 4 项：
1. ✅ Wi-Fi Direct P2P 协议握手成功（Windows `WiFiDirectConnectionListener.ConnectionRequested` 触发，拿到对端 deviceInfo + IP）
2. ✅ 双方进入同一 P2P group，DHCP 分配同子网 IP（GO 192.168.137.1 / Client 192.168.137.247）
3. ✅ IP 层互通（ICMP ping 双向通）
4. ✅ TCP 应用层数据通过 Wi-Fi Direct 链路抵达（HELLO 消息收到）

## 关键技术决策（已落地）

| 决策点 | 选择 | 理由 |
|---|---|---|
| Windows API | `Windows.Devices.WiFiDirect`（C++/WinRT） | Win10/11 唯一活跃的 P2P API |
| Android API | `WifiP2pManager` + WPS PBC | Wi-Fi Direct 协议互通的唯一路径 |
| Group Owner | Windows 端固定当 GO（autonomous, intent=14） | IP 固定 192.168.137.1，可预期 |
| Pairing 模式 | PBC（推按钮，无 PIN） | 用户体验好，autonomous GO 自动接受 |
| Legacy SoftAP | **关闭**（`LegacySettings.IsEnabled(false)`） | AX201 驱动不支持 Hosted Network，开了 publisher 假 Started 但不发 beacon |
| Peer IP 来源 | Windows `WiFiDirectDevice.GetConnectionEndpointPairs()` | legacy 关时这是唯一可靠路径 |
| 传输 | TCP socket，line-based 文本协议 | 阶段 1 简化验证 |

## 已知问题（非 demo 阻塞，留 stage 2 一并解决）

### 问题 1：EMUI 上 `WIFI_P2P_CONNECTION_CHANGED_ACTION` broadcast 时序

`WifiP2pManager.connect()` 成功 + Windows 端 `peer joined` 已触发，但 Android 这边的 `EXTRA_NETWORK_INFO.isConnected` 在 broadcast 中**未稳定上报**，导致 `MainActivity.onConnected()` 不会被回调，进而 `TcpClient.connect()` 不会自动发起。

**影响**：app UI 上 SEND TEST 按钮一直 disabled。

**workaround（待实现）**：把 `WifiP2pClient.kt:55-66` 改成不依赖 `EXTRA_NETWORK_INFO`，每次收到 `WIFI_P2P_CONNECTION_CHANGED_ACTION` 都无条件 `requestConnectionInfo`，回调里看 `info.groupFormed` 才认为连上。

### 问题 2：AX201 不支持 STA + GO 共存

`netsh wlan show drivers` 报告 `Hosted network supported : No`，配合实测：**Windows 必须断开当前 Wi-Fi 才能起 P2P virtual adapter**。流程上 GO Start 时如果还连着 STA，publisher status 会停在 `1 (Started)` 但 P2P virtual adapter 不上线，手机扫不到/连不上。

**workaround**：跑 demo 前先 `netsh wlan disconnect interface="WLAN"`，跑完再 `netsh wlan connect name="<profile>"` 恢复。当前 main.cpp 没自动处理这一步——使用者要先手动断 Wi-Fi。

### 问题 3：Windows GO 进程"echo 后退出"模式

`tcp_server.cpp::RunStage1Echo` 是 client 断开就 return → main 退出。多客户端测试时要重新启动 EXE。这是 demo 阶段的极简实现，stage 2 视频流自然会改成长连接。

## 编译/部署

完整流程见 `docs/DEPLOY.md`。简版：

```powershell
# Windows 端（管理员 PowerShell；先断 Wi-Fi）
netsh wlan disconnect interface="WLAN"
cd E:\work\wifip2p\windows-app\build
cmake --build . --config Release
.\Release\wifip2p_windows.exe

# Android 端（adb 装好后）
.\android-app\gradlew assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb shell pm grant com.hermes.wifip2p android.permission.ACCESS_FINE_LOCATION
adb shell am start -n com.hermes.wifip2p/.MainActivity
# UI 上 tap DISCOVER → tap PC name (DESKTOP-*)
```

## Stage 2：Android 屏幕投屏到 Windows（H.264 over Wi-Fi Direct）

**前提**：Stage 1 链路证明 Wi-Fi Direct 能承载 TCP 数据，Stage 2 把数据从文本换成 H.264 NAL 字节流。

### Android 端要加：
- `MediaProjectionManager.createScreenCaptureIntent()` + 前台服务（Android 14+ 强制）
- `MediaCodec` 编码器（colorFormat=Surface，input 接 VirtualDisplay）：H.264 720p30 4Mbps
- 把 codec 输出的 NAL 字节流（带 SPS/PPS）写到现有 `TcpClient` socket
- 协议升级到 length-prefixed frame：`[uint32 BE size][bytes NAL units]`

### Windows 端要加：
- 接收 length-prefixed frame
- MediaFoundation H.264 解码器（参考 `mfdecodedemo` 项目，CleanPoint/IDR、NV12 ring 那些坑都已知）
- D3D11 渲染到 HWND（同 mfdecodedemo 模式）
- 修 `tcp_server.cpp` 改成长连接 + frame-based 读

### 修复 Stage 1 遗留问题：
- 改 `WifiP2pClient.kt` 不依赖 `EXTRA_NETWORK_INFO`（问题 1）
- 在 main.cpp 启动时自动断 STA、退出时恢复 STA（问题 2）
- TCP server 改成长连接 + 异常重连（问题 3）

## 文件清单

```
E:\work\wifip2p\
├── windows-app\                         C++/WinRT, CMake + VS2022
│   ├── CMakeLists.txt
│   ├── src\
│   │   ├── main.cpp                     启动 GO + TCP server
│   │   ├── wifi_direct_go.{h,cpp}       WiFiDirectAdvertisementPublisher 封装
│   │   ├── tcp_server.{h,cpp}           accept + line echo
│   │   └── log.h                        dual-sink 文件日志（与 EXE 同目录 wifip2p.log）
│   ├── build\Release\wifip2p_windows.exe   65KB（编译产物）
│   └── README.md
├── android-app\                         Kotlin, AGP 8.2, Gradle 8.5, JDK 21
│   └── app\src\main\
│       ├── java\com\hermes\wifip2p\
│       │   ├── MainActivity.kt
│       │   ├── WifiP2pClient.kt         WifiP2pManager 包装
│       │   └── TcpClient.kt
│       └── ...
├── docs\
│   ├── PROTOCOL.md                      socket 字节流协议
│   └── DEPLOY.md                        部署 + 配对流程
├── README.md                            顶层导航
└── STATUS.md                            本文件
```
