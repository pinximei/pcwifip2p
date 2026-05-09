# pcwifip2p

PC ↔ Android 的 Wi-Fi Direct 直连 demo。Windows 当 Group Owner（autonomous，PBC），Android 用原生 `WifiP2pManager` 加入。Group 建立后通过 TCP 跑应用层消息，**两端都有 GUI**，可以互相手输文本验证双向通信。

```
+----------------------+        Wi-Fi Direct (PBC)         +----------------------+
|   wifip2p_           |  <------------------------------> |   wifip2p Android    |
|   windows.exe        |     Win 当 GO, intent=14          |   (Kotlin)           |
|   C++/WinRT + Win32  |                                   |   WifiP2pManager     |
+----------|-----------+                                   +-----------|----------+
           |   TCP :8888（group 建立后）                                |
           +-----------------------------------------------------------+
                            Stage 1: 行式文本回显 (✅ 实测通过)
                            Stage 2: 裸 H.264 NAL 投屏 (未做)
```

## 当前状态

| Stage | 目标 | 状态 |
|---|---|---|
| **1** | PC 与 Android Wi-Fi Direct 配对、TCP 双向手输消息 | ✅ **实测通过**（DT2002C / OPPO ColorOS） |
| **2** | Android 屏幕 MediaProjection + MediaCodec H.264 → Windows MediaFoundation 解码 + D3D11 渲染 | 未开始 |

实测过的关键事实，避免你后续踩坑：

- **PC Wi-Fi 网卡必须支持 Wi-Fi Direct**。Intel AX 系列驱动**默认不支持** SoftAP / Hosted Network，但**支持** 真 P2P 协议——这个 demo 走真 P2P，不依赖 SoftAP
- **Windows EXE 必须管理员启动**——`WiFiDirectAdvertisementPublisher` autonomous GO 需要 admin token
- **EMUI（华为）的 P2P 实现限制多**——切到 ColorOS / 原生 Android / 锤子 OS 上一次过
- 真 P2P 模式下 `LegacySettings.IsEnabled = false`，`ConnectionRequested` 事件**通常不会 fire**，对端 IP 从 TCP `accept()` 拿
- ColorOS 偶发 "半挂断"——`dumpsys wifip2p` 显示 `GroupCreatedState`，但 `ping 192.168.137.1` 100% loss。重置方法：`adb shell svc wifi disable && svc wifi enable`，不用关 PC 的 Wi-Fi

## 目录结构

```
pcwifip2p/
├── windows-app/         C++/WinRT + Win32 GUI EXE（GO + TCP server）
│   ├── src/
│   │   ├── main.cpp           Win32 GUI 主窗口（输入框 + Send 按钮 + 收发日志）
│   │   ├── wifi_direct_go.cpp WiFiDirectAdvertisementPublisher 起 GO
│   │   ├── tcp_server.cpp     TCP server + 行式协议
│   │   └── log.cpp            stdout + 文件双 sink
│   └── CMakeLists.txt
├── android-app/         Kotlin app（P2P client + TCP client）
│   ├── app/src/main/
│   │   ├── java/com/hermes/wifip2p/
│   │   │   ├── MainActivity.kt        UI + 按钮事件
│   │   │   ├── WifiP2pClient.kt       WifiP2pManager 包装
│   │   │   └── TcpClient.kt           TCP 收发
│   │   └── res/layout/activity_main.xml
│   └── build.gradle.kts
├── docs/
│   ├── PROTOCOL.md      行式 wire format（Stage 1 已用，Stage 2 预留）
│   └── DEPLOY.md        构建 + 安装 + 烟测流程
├── .github/workflows/
│   ├── build-windows.yml   手动触发，编 Windows EXE
│   └── build-android.yml   手动触发，编 Android APK
└── README.md
```

## CI / GitHub Actions

两条 workflow，都是 **手动触发**（`workflow_dispatch`），需要时去仓库 Actions 页面点 "Run workflow"。

| Workflow | 触发 | 产物 |
|---|---|---|
| `build-windows.yml` | 手动 | `wifip2p_windows.exe` (Release/x64) |
| `build-android.yml` | 手动 | `app-debug.apk` |

触发步骤：

1. 打开仓库 → **Actions** 标签
2. 左侧选 `build-windows` 或 `build-android`
3. 右上角 **Run workflow** → 选分支 → 点绿色按钮
4. 跑完后到 workflow run 详情页，最下方 **Artifacts** 区下载 EXE / APK

## 本地构建

### Windows 端（PC 上跑，必须 VS2022 + Win10/11 SDK 22621）

```powershell
cd windows-app
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# 产物：build\Release\wifip2p_windows.exe
```

### Android 端（PC 上跑，需要 Android Studio 自带 JBR + Android SDK 34）

```powershell
cd android-app
.\gradlew.bat assembleDebug
# 产物：app\build\outputs\apk\debug\app-debug.apk
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

## 跑起来（端到端 ~30 秒）

1. **以管理员**启动 `wifip2p_windows.exe`——会出现一个 Win32 GUI 窗口（标题 `wifip2p Windows GO (Stage 1)`），底部有输入框 + Send 按钮
2. 手机打开 wifip2p app → **DISCOVER** → peer 列表里点 PC（认 MAC `f8:5e:a0:...`）→ 等到 `group formed`
3. App 自动 TCP 连接 192.168.137.1:8888，状态条变成 `connected to 192.168.137.1`
4. **PC 输入框输文字** → Send → 手机 logView 立刻看到 `tcp recv: 你输的文字`
5. **手机输入框输文字** → SEND → PC 窗口 list 立刻看到 `<< 你输的文字`

详细排错见 [`docs/DEPLOY.md`](docs/DEPLOY.md)。

## 协议（Stage 1）

行式文本，UTF-8，`\n` 分隔。Server 行为：

| Client → Server | Server → Client |
|---|---|
| `HELLO <name>` | `WELCOME <name>` |
| `PING` | `PONG` |
| 任意其他 | `ECHO <原文>` |

Server 主动发的：连接建立时回 greeting `WIFIP2P-DEMO/1`。

完整 wire format（含 Stage 2 预留的长度前缀 NAL 帧格式）见 [`docs/PROTOCOL.md`](docs/PROTOCOL.md)。

## 架构选择（一句话理由）

- **Windows 当 GO**：IP 固定 `192.168.137.1`，Server 一头，Client 一头，最简
- **PBC 配对**：用户体验好，不用输 PIN
- **真 Wi-Fi Direct P2P**（不是 SoftAP / Hosted Network）：Intel AX 网卡新驱动只支持这条路
- **裸 TCP**：单跳 Wi-Fi Direct 链路 RTT ~40ms，TCP head-of-line blocking 不是问题
- **不用 Miracast / RTSP / Cast SDK**：本项目验证 Wi-Fi Direct **链路**本身，不是要做投屏产品
