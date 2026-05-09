# wifip2p — deploy & smoke test

## One-time prerequisites

**PC (Windows 10/11)**
- VS 2022 with "Desktop development with C++" workload (already verified —
  used by sysvad build)
- CMake ≥ 3.20
- Wi-Fi adapter present and enabled (Wi-Fi Direct rides on the same radio)

**Phone (Huawei FOA-LX9 or any Android 7.0+)**
- Android Studio installed locally so `gradlew.bat` can resolve toolchain
- USB debugging enabled, device authorized to `E:\platform-tools\adb.exe`
- Wi-Fi enabled (it can stay associated to a normal AP — Wi-Fi Direct
  coexists; some Huawei builds will briefly disconnect during P2P
  negotiation, that's expected)

## Build

```powershell
# Windows app
cd E:\work\wifip2p\windows-app
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# -> build\Release\wifip2p_windows.exe

# Android app
cd E:\work\wifip2p\android-app
.\gradlew.bat assembleDebug
# -> app\build\outputs\apk\debug\app-debug.apk
```

## Install on phone

```powershell
E:\platform-tools\adb.exe devices              # confirm FOA-LX9 is "device"
E:\platform-tools\adb.exe install -r app\build\outputs\apk\debug\app-debug.apk
```

## Run

1. **PC**: launch `wifip2p_windows.exe` from an *Administrator* PowerShell.
   The console prints the legacy SoftAP SSID + passphrase.
2. **Phone**: open the wifip2p app. Grant location + nearby-wifi perms.
   Tap **Discover** — the PC should appear within 5–10s.
3. Tap the PC entry. Confirm any PBC prompt that pops on Windows.
4. Once the group forms, the phone auto-opens TCP to the GO IP `:8888`
   and sends `HELLO`. The PC console should log `[TCP][rx] HELLO ...`.
5. Tap **Send test** a few times — verify `PONG`/`ECHO ...` lines come back.

## Troubleshooting

- **Discover finds nothing**: open Windows Settings → Bluetooth & devices →
  Add device → "Everything else", verify the WD entry exists. If absent,
  the legacy SoftAP failed to start (often due to driver capability gaps —
  some Realtek chipsets refuse autonomous-GO). Run as Administrator and
  check `Get-NetAdapter -Physical | where InterfaceDescription -match Wi-Fi`.
- **Connect fails with reason=2 (BUSY)**: phone is already in another P2P
  group. Settings → More connections → Wi-Fi Direct → disconnect, retry.
- **Connect succeeds but TCP times out**: check Windows Defender Firewall
  isn't blocking inbound on port 8888 for the new "Microsoft Wi-Fi Direct
  Virtual Adapter" interface — usually auto-prompts on first run.
- **Huawei `groupOwnerIntent` is ignored**: EMUI sometimes overrides. If
  the phone becomes GO instead of Windows, the app logs a warning and the
  TCP step is skipped — disconnect and retry; relocking by deleting saved
  P2P group on phone usually fixes it.

## Logs

```powershell
# Phone
E:\platform-tools\adb.exe logcat -s wifip2p:* AndroidRuntime:E

# PC: just read the EXE console
```
