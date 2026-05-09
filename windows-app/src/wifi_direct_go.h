#pragma once
#include <string>
#include <functional>

namespace wifip2p {

// Starts a Wi-Fi Direct soft-AP (Group Owner) in PBC mode.
// Blocks until either a peer connects or Stop() is called.
// On peer connect, on_peer_connected is invoked with the peer's IPv4 string.
class WifiDirectGo {
public:
    WifiDirectGo();
    ~WifiDirectGo();

    // Starts the advertiser. Returns false on init failure.
    // ssid_out / passphrase_out filled if available (PBC may omit passphrase).
    bool Start(std::wstring& ssid_out, std::wstring& passphrase_out);

    // Blocks until a peer connects or Stop() is called.
    // Returns peer IPv4 string on success, empty on cancel.
    std::wstring WaitForPeer();

    void Stop();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace wifip2p
