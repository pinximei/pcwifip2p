#include "wifi_direct_go.h"
#include "log.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.WiFiDirect.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <winrt/Windows.Security.Credentials.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Devices::WiFiDirect;
using namespace Windows::Networking;
using namespace Windows::Networking::Connectivity;

namespace wifip2p {

struct WifiDirectGo::Impl {
    WiFiDirectAdvertisementPublisher publisher{ nullptr };
    WiFiDirectConnectionListener listener{ nullptr };
    event_token status_token{};
    event_token conn_token{};

    std::mutex mu;
    std::condition_variable cv;
    std::wstring peer_ip;
    bool stopped = false;
};

WifiDirectGo::WifiDirectGo() : impl_(new Impl()) {}

WifiDirectGo::~WifiDirectGo() {
    Stop();
    delete impl_;
}

bool WifiDirectGo::Start(std::wstring& ssid_out, std::wstring& passphrase_out) {
    try {
        impl_->publisher = WiFiDirectAdvertisementPublisher();
        auto adv = impl_->publisher.Advertisement();

        // Become a Group Owner (autonomous mode), PBC pairing.
        adv.IsAutonomousGroupOwnerEnabled(true);

        // NOTE: legacy SoftAP mode requires WLAN Hosted Network support in the Wi-Fi driver.
        // On adapters where `netsh wlan show drivers` reports "Hosted network supported : No"
        // (common on modern Intel/Killer/Realtek chips since Win10 1607), enabling legacy mode
        // makes publisher.Start() fail. Use real Wi-Fi Direct P2P instead — Android side joins
        // via WifiP2pManager.discoverPeers() + connect() (PBC). Peer IP comes from the
        // WiFiDirectConnectionListener.ConnectionRequested handler below.
        WiFiDirectLegacySettings legacy = adv.LegacySettings();
        legacy.IsEnabled(false);

        impl_->listener = WiFiDirectConnectionListener();

        impl_->status_token = impl_->publisher.StatusChanged(
            [](WiFiDirectAdvertisementPublisher const&, WiFiDirectAdvertisementPublisherStatusChangedEventArgs const& args) {
                LOGI("[GO] publisher status: %d", (int)args.Status());
            });

        // P2P-pairing path: this is now the primary path (legacy disabled).
        // Android's WifiP2pManager.connect() triggers WPS PBC -> this handler fires.
        impl_->conn_token = impl_->listener.ConnectionRequested(
            [this](WiFiDirectConnectionListener const&, WiFiDirectConnectionRequestedEventArgs const& args) -> fire_and_forget {
                try {
                    auto request = args.GetConnectionRequest();
                    auto device_info = request.DeviceInformation();
                    LOGI("[GO] (p2p) peer connecting: %s",
                         WideToUtf8(std::wstring(device_info.Name())).c_str());

                    auto device = co_await WiFiDirectDevice::FromIdAsync(device_info.Id());
                    auto endpoints = device.GetConnectionEndpointPairs();
                    if (endpoints.Size() > 0) {
                        std::wstring ip = std::wstring(endpoints.GetAt(0).RemoteHostName().CanonicalName());
                        {
                            std::lock_guard<std::mutex> lk(impl_->mu);
                            impl_->peer_ip = ip;
                        }
                        impl_->cv.notify_all();
                        LOGI("[GO] (p2p) peer joined, ip=%s", WideToUtf8(ip).c_str());
                    }
                } catch (winrt::hresult_error const& e) {
                    LOGE("[GO] (p2p) connect handler error: %s",
                         WideToUtf8(std::wstring(e.message())).c_str());
                }
            });

        impl_->publisher.Start();

        // SSID/passphrase only meaningful in legacy mode; in P2P mode pairing is PBC.
        ssid_out.clear();
        passphrase_out.clear();

        return true;
    } catch (winrt::hresult_error const& e) {
        LOGE("[GO] start failed: 0x%08X %s",
             (unsigned)e.code(),
             WideToUtf8(std::wstring(e.message())).c_str());
        return false;
    }
}

std::wstring WifiDirectGo::WaitForPeer() {
    std::unique_lock<std::mutex> lk(impl_->mu);
    impl_->cv.wait(lk, [this] { return impl_->stopped || !impl_->peer_ip.empty(); });
    return impl_->peer_ip;
}

void WifiDirectGo::Stop() {
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->stopped = true;
    }
    impl_->cv.notify_all();
    try {
        if (impl_->publisher) {
            if (impl_->status_token.value) impl_->publisher.StatusChanged(impl_->status_token);
            impl_->publisher.Stop();
        }
        if (impl_->listener && impl_->conn_token.value) {
            impl_->listener.ConnectionRequested(impl_->conn_token);
        }
    } catch (...) {}
}

} // namespace wifip2p
