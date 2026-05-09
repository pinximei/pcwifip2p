# wifip2p

Wi-Fi Direct demo between a Windows PC (Group Owner) and an Android phone.

```
+------------------+         Wi-Fi Direct (PBC)         +------------------+
|   wifip2p_       |  <-------------------------------> |   wifip2p        |
|   windows.exe    |    GO intent=14, legacy SoftAP     |   (Android app)  |
|   C++/WinRT      |                                    |   Kotlin / WifiP2pManager |
+--------|---------+                                    +---------|--------+
         |  TCP :8888 (after group formed)                        |
         +--------------------------------------------------------+
                       Stage 1 : line-based echo
                       Stage 2 : raw H.264 NAL  (TODO)
```

## Layout

```
wifip2p/
├── windows-app/    C++/WinRT EXE  (Group Owner + TCP server)
├── android-app/    Kotlin app     (P2P client + TCP client)
├── docs/
│   ├── PROTOCOL.md   wire format
│   └── DEPLOY.md     build + install + smoke test
└── README.md
```

## Stages

| Stage | Goal | Status |
|-------|------|--------|
| **1** | Win + Android pair via Wi-Fi Direct, exchange line-based messages over TCP | scaffold complete, awaiting on-device verification |
| **2** | Android `MediaProjection` + `MediaCodec` H.264 → Win MediaFoundation/D3D11 render, on the same Wi-Fi Direct link | not started |

## Quick start

```powershell
# build PC side
cd windows-app
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

# build + install phone side (USB only used for adb, not at runtime)
cd ..\android-app
.\gradlew.bat assembleDebug
E:\platform-tools\adb.exe install -r app\build\outputs\apk\debug\app-debug.apk

# run
.\..\windows-app\build\Release\wifip2p_windows.exe   # as Administrator
# then on phone: open app -> Discover -> tap PC -> Send test
```

Full instructions: [`docs/DEPLOY.md`](docs/DEPLOY.md).

## Architectural choices

- **Roles**: Windows is the **Group Owner** (`IsAutonomousGroupOwnerEnabled=true`,
  `GroupOwnerIntent=14`). Predictable IP, simpler server logic, fewer Huawei
  EMUI surprises.
- **Pairing**: **Push Button (PBC)** — no PIN entry; user taps once on each
  side.
- **Transport**: plain TCP. UDP/QUIC overkill for the demo; H.264 is
  resilient to TCP head-of-line blocking on a single-hop Wi-Fi Direct link.
- **No screen casting frameworks**: we deliberately do *not* use Miracast /
  RTSP / Cast SDK. The point of this project is to validate the
  Wi-Fi Direct **link**, not to compete with a media-grade stack.
