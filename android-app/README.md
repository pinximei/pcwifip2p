# wifip2p — Android app

Kotlin demo client. Joins the Windows Group Owner over Wi-Fi Direct (PBC),
opens a TCP socket to the GO IP, and talks the line-based WIFIP2P-DEMO/1
protocol.

## Requirements

- Android Studio Iguana+ (AGP 8.2.2 + Gradle 8.5 + Kotlin 1.9.22)
- JDK 17 or higher (Android Studio's bundled JBR 21 works — `gradlew.bat`
  picks it up via `JAVA_HOME` if set, otherwise via Studio at sync time)
- Android SDK platform 34 installed (the project's `compileSdk` / `targetSdk`)
- Test device: Android 7.0 (API 24) or higher with Wi-Fi Direct support
  (your Huawei FOA-LX9 / Android 12 / API 31 is fine)

This repo's `local.properties` points at `D:\Androidstudio` for the SDK.
If your SDK lives elsewhere, edit it before opening in Studio.

## Build & install via USB

```powershell
cd E:\work\wifip2p\android-app
.\gradlew.bat assembleDebug
E:\platform-tools\adb.exe install -r app\build\outputs\apk\debug\app-debug.apk
```

USB stays plugged in only for `adb install` and `adb logcat`. Wi-Fi Direct
runs over the phone's Wi-Fi radio and does not depend on the USB cable.

## Run

1. Start `wifip2p_windows.exe` on PC (as Administrator) — note the SSID.
2. Open the **wifip2p** app on the phone, grant location/nearby-wifi perms.
3. Tap **Discover**, select the PC entry, tap to connect (or join the SSID
   manually from system Wi-Fi if discovery is flaky on your ROM).
4. App auto-opens TCP to GO IP `:8888` and sends `HELLO android-<model>`.
5. **Send test** alternates `PING`/`tick …` and shows `PONG`/`ECHO …` from PC.

## Logs

```powershell
E:\platform-tools\adb.exe logcat -s wifip2p:* AndroidRuntime:E
```
