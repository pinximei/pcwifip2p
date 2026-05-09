#pragma once
// Tiny dual-sink logger: writes to stdout AND a log file beside the EXE.
// Use UTF-8 narrow strings only -- avoids the wcout-on-redirected-stdout
// silence trap.

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace wifip2p {

inline std::wstring LogPath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p.resize(pos + 1);
    p += L"wifip2p.log";
    return p;
}

inline FILE* LogFile() {
    static std::mutex mu;
    static FILE* f = nullptr;
    static bool tried = false;
    std::lock_guard<std::mutex> lk(mu);
    if (!tried) {
        tried = true;
        auto path = LogPath();
        // _wfsopen with _SH_DENYNO so we and tail can read concurrently.
        f = _wfsopen(path.c_str(), L"w", 0x40 /* _SH_DENYNO */);
        if (f) {
            setvbuf(f, nullptr, _IONBF, 0); // line/no buffering
        }
    }
    return f;
}

inline void LogLine(const char* tag, const char* fmt, va_list ap) {
    char body[2048];
    vsnprintf(body, sizeof(body), fmt, ap);
    body[sizeof(body) - 1] = 0;

    SYSTEMTIME st; GetLocalTime(&st);
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "[%02d:%02d:%02d.%03d] %s ",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, tag);

    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);

    fputs(hdr, stdout); fputs(body, stdout); fputc('\n', stdout); fflush(stdout);
    if (FILE* f = LogFile()) {
        fputs(hdr, f); fputs(body, f); fputc('\n', f); fflush(f);
    }
}

inline void LogI(const char* fmt, ...) { va_list ap; va_start(ap, fmt); LogLine("I", fmt, ap); va_end(ap); }
inline void LogE(const char* fmt, ...) { va_list ap; va_start(ap, fmt); LogLine("E", fmt, ap); va_end(ap); }

inline std::string WideToUtf8(std::wstring const& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace wifip2p

#define LOGI(...) ::wifip2p::LogI(__VA_ARGS__)
#define LOGE(...) ::wifip2p::LogE(__VA_ARGS__)
