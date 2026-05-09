# wifip2p — Windows app

Native C++/WinRT Wi-Fi Direct Group Owner + TCP server for the demo.

## Build

```powershell
cd E:\work\wifip2p\windows-app
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output: `build\Release\wifip2p_windows.exe`

## Run

1. Make sure the PC has Wi-Fi enabled (Wi-Fi Direct rides on the Wi-Fi adapter).
2. Run `wifip2p_windows.exe` as **Administrator** (some adapters require it
   for `WiFiDirectAdvertisementPublisher` autonomous mode).
3. The app starts an autonomous Group Owner and prints a legacy SSID/passphrase
   if available — Android can also discover it via Wi-Fi Direct peer scan.
4. On Android: open the app, tap **Connect**. Confirm the PBC prompt on Windows
   (Action Center / "Add device" toast) within 2 minutes.
5. Once peer joins, the app accepts a TCP connection on `:8888` and echoes
   any bytes the Android side sends.

## Notes

- Stage 1 only validates the link (handshake + echo). Stage 2 will replace the
  echo server with an H.264 NAL receiver + MediaFoundation/D3D11 renderer.
- Group Owner intent is hard-coded to 14 (max) so Windows always wins GO
  negotiation.
