#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace wifip2p {

// Minimal TCP server for the demo, GUI-friendly.
// - AcceptOne blocks until exactly one Android peer connects.
// - Run() then loops on recv, calling on_line(line) for each line received.
// - Send(line) is thread-safe relative to Run() (single sender thread is fine;
//   we use it from the UI thread, while Run() executes on the worker thread).
class TcpServer {
public:
    using LineCallback = std::function<void(const std::string&)>;
    using StatusCallback = std::function<void(const std::string&)>;

    // Bind listener (first call) or just accept next client (subsequent calls).
    bool AcceptOne(uint16_t port, std::string* peer_ip = nullptr);

    // Send greeting + reply protocol entry; calls on_line for every received
    // line and on_status for connect/disconnect milestones.
    void Run(LineCallback on_line, StatusCallback on_status);

    // Send a raw line (no '\n' needed; we append it). Safe to call from UI thread.
    bool SendLine(const std::string& line);

    // Close just the per-client socket (keeps listener alive for re-accept).
    void CloseClient();

    void Close();

    bool HasClient() const { return client_sock_ != ~uintptr_t(0); }

private:
    uintptr_t listen_sock_ = ~uintptr_t(0);
    uintptr_t client_sock_ = ~uintptr_t(0);
};

} // namespace wifip2p
