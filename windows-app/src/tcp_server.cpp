#include "tcp_server.h"
#include "log.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <string>
#include <mutex>

#pragma comment(lib, "Ws2_32.lib")

namespace wifip2p {

namespace {
struct WinsockInit {
    WinsockInit() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WinsockInit() { WSACleanup(); }
};
WinsockInit g_wsa;

std::mutex g_send_mtx;
} // namespace

bool TcpServer::AcceptOne(uint16_t port, std::string* peer_ip) {
    if (listen_sock_ == (uintptr_t)INVALID_SOCKET) {
        listen_sock_ = (uintptr_t)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_sock_ == (uintptr_t)INVALID_SOCKET) {
            LOGE("[TCP] socket() failed: %d", WSAGetLastError());
            return false;
        }

        BOOL reuse = TRUE;
        setsockopt((SOCKET)listen_sock_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind((SOCKET)listen_sock_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            LOGE("[TCP] bind(%u) failed: %d", (unsigned)port, WSAGetLastError());
            return false;
        }
        if (listen((SOCKET)listen_sock_, 4) == SOCKET_ERROR) {
            LOGE("[TCP] listen failed: %d", WSAGetLastError());
            return false;
        }
        LOGI("[TCP] listening on 0.0.0.0:%u, waiting for client...", (unsigned)port);
    } else {
        LOGI("[TCP] re-accepting on 0.0.0.0:%u, waiting for client...", (unsigned)port);
    }

    sockaddr_in peer{};
    int peer_len = sizeof(peer);
    SOCKET cs = accept((SOCKET)listen_sock_, (sockaddr*)&peer, &peer_len);
    if (cs == INVALID_SOCKET) {
        LOGE("[TCP] accept failed: %d", WSAGetLastError());
        return false;
    }

    char ipbuf[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &peer.sin_addr, ipbuf, sizeof(ipbuf));
    LOGI("[TCP] client connected from %s:%u", ipbuf, (unsigned)ntohs(peer.sin_port));

    BOOL nodelay = TRUE;
    setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));

    BOOL ka = TRUE;
    setsockopt(cs, SOL_SOCKET, SO_KEEPALIVE, (const char*)&ka, sizeof(ka));
    DWORD bytes = 0;
    tcp_keepalive ka_args{};
    ka_args.onoff = 1;
    ka_args.keepalivetime = 5000;
    ka_args.keepaliveinterval = 1000;
    WSAIoctl(cs, SIO_KEEPALIVE_VALS, &ka_args, sizeof(ka_args), nullptr, 0, &bytes, nullptr, nullptr);

    if (peer_ip) *peer_ip = ipbuf;
    client_sock_ = (uintptr_t)cs;
    return true;
}

void TcpServer::Run(LineCallback on_line, StatusCallback on_status) {
    if (client_sock_ == (uintptr_t)INVALID_SOCKET) return;
    SOCKET cs = (SOCKET)client_sock_;

    // Send greeting on connect.
    SendLine("WIFIP2P-DEMO/1");
    if (on_status) on_status("client connected");

    std::string buf;
    buf.reserve(2048);
    char chunk[512];
    while (true) {
        int n = recv(cs, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            LOGI("[TCP] client disconnected (recv=%d)", n);
            if (on_status) on_status("client disconnected");
            break;
        }
        buf.append(chunk, n);

        size_t pos;
        while ((pos = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, pos);
            buf.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();

            LOGI("[TCP][rx] %s", line.c_str());
            if (on_line) on_line(line);
        }
    }
}

bool TcpServer::SendLine(const std::string& line) {
    if (client_sock_ == (uintptr_t)INVALID_SOCKET) return false;
    std::lock_guard<std::mutex> lk(g_send_mtx);
    SOCKET cs = (SOCKET)client_sock_;
    std::string out = line;
    if (out.empty() || out.back() != '\n') out.push_back('\n');
    int sent = send(cs, out.c_str(), (int)out.size(), 0);
    if (sent <= 0) {
        LOGE("[TCP] send failed: %d", WSAGetLastError());
        return false;
    }
    LOGI("[TCP][tx] %s", line.c_str());
    return true;
}

void TcpServer::CloseClient() {
    std::lock_guard<std::mutex> lk(g_send_mtx);
    if (client_sock_ != (uintptr_t)INVALID_SOCKET) {
        closesocket((SOCKET)client_sock_);
        client_sock_ = (uintptr_t)INVALID_SOCKET;
    }
}

void TcpServer::Close() {
    CloseClient();
    if (listen_sock_ != (uintptr_t)INVALID_SOCKET) {
        closesocket((SOCKET)listen_sock_);
        listen_sock_ = (uintptr_t)INVALID_SOCKET;
    }
}

} // namespace wifip2p
