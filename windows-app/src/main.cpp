// wifip2p Windows demo - Group Owner side, Win32 GUI version.
//
// Layout:
//   [ status bar ]
//   [ log list view (rx/tx history) ............................ ]
//   [ input edit ......................................[ Send ] ]
//
// GO + TCP runs in a worker thread; UI events posted via PostMessage.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <winrt/base.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <deque>

#include "log.h"
#include "wifi_direct_go.h"
#include "tcp_server.h"

#pragma comment(lib, "Comctl32.lib")
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

constexpr uint16_t kTcpPort = 8888;

// Custom messages.
constexpr UINT WM_APP_LOG    = WM_APP + 1;  // wParam = unused, lParam = char* (heap-allocated UTF-8)
constexpr UINT WM_APP_STATUS = WM_APP + 2;  // lParam = char* (heap-allocated UTF-8)

// UI handles (single-window app, globals OK).
static HWND g_hwnd      = nullptr;
static HWND g_hStatus   = nullptr;
static HWND g_hList     = nullptr;
static HWND g_hInput    = nullptr;
static HWND g_hBtnSend  = nullptr;

static wifip2p::WifiDirectGo* g_go = nullptr;
static wifip2p::TcpServer*   g_srv = nullptr;
static std::atomic<bool>     g_clientConnected{false};
static std::thread           g_worker;

// ---- helpers ---------------------------------------------------------------

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static void PostLog(const std::string& tag, const std::string& msg) {
    SYSTEMTIME st; GetLocalTime(&st);
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "[%02d:%02d:%02d] %s ",
             st.wHour, st.wMinute, st.wSecond, tag.c_str());
    std::string full = std::string(hdr) + msg;
    char* heap = _strdup(full.c_str());
    PostMessageW(g_hwnd, WM_APP_LOG, 0, (LPARAM)heap);
}

static void PostStatus(const std::string& s) {
    char* heap = _strdup(s.c_str());
    PostMessageW(g_hwnd, WM_APP_STATUS, 0, (LPARAM)heap);
}

static void AppendListLine(const std::wstring& w) {
    int idx = (int)SendMessageW(g_hList, LB_ADDSTRING, 0, (LPARAM)w.c_str());
    // Cap to last 500 lines.
    int count = (int)SendMessageW(g_hList, LB_GETCOUNT, 0, 0);
    while (count > 500) {
        SendMessageW(g_hList, LB_DELETESTRING, 0, 0);
        count--;
    }
    // Auto-scroll to bottom.
    SendMessageW(g_hList, LB_SETTOPINDEX, idx, 0);
}

// ---- worker thread ---------------------------------------------------------

static void WorkerMain() {
    PostStatus("Starting Wi-Fi Direct GO...");
    PostLog("--", "=== wifip2p Windows GO demo ===");
    PostLog("--", "Role: Group Owner (autonomous, PBC)");

    g_go = new wifip2p::WifiDirectGo();
    std::wstring ssid, pw;
    if (!g_go->Start(ssid, pw)) {
        PostStatus("ERROR: Wi-Fi Direct start failed (need admin?)");
        PostLog("E ", "Wi-Fi Direct advertiser start failed");
        return;
    }
    PostLog("--", "Wi-Fi Direct advertiser running.");

    g_srv = new wifip2p::TcpServer();
    PostStatus("Wi-Fi Direct GO up. Waiting for Android peer + TCP...");
    PostLog("--", "TCP listening on 0.0.0.0:" + std::to_string((unsigned)kTcpPort));

    while (true) {
        std::string peer_ip;
        if (!g_srv->AcceptOne(kTcpPort, &peer_ip)) {
            PostStatus("ERROR: TCP accept failed");
            PostLog("E ", "TCP accept failed");
            return;
        }
        PostStatus("Peer connected: " + peer_ip);
        PostLog("--", "TCP peer connected from " + peer_ip + ":" + std::to_string(kTcpPort));
        g_clientConnected = true;

        // Enable Send button on UI thread.
        PostMessageW(g_hwnd, WM_APP + 10, 0, 0);

        g_srv->Run(
            [](const std::string& line) { PostLog("<<", line); },
            [](const std::string& s)    { PostStatus(s); }
        );

        g_clientConnected = false;
        PostMessageW(g_hwnd, WM_APP + 11, 0, 0); // disable send
        g_srv->CloseClient();
        PostLog("--", "Session ended. Waiting for next client...");
        PostStatus("Wi-Fi Direct GO up. Waiting for next Android peer...");
    }
}

// ---- UI --------------------------------------------------------------------

static void DoSend() {
    wchar_t buf[1024];
    int n = GetWindowTextW(g_hInput, buf, _countof(buf));
    if (n == 0) {
        PostLog("INFO", "(empty input, nothing sent)");
        return;
    }
    std::wstring w(buf, n);
    // strip trailing whitespace
    while (!w.empty() && (w.back() == L' ' || w.back() == L'\t' || w.back() == L'\r' || w.back() == L'\n'))
        w.pop_back();
    if (w.empty()) return;

    int blen = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(blen, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), blen, nullptr, nullptr);

    if (!g_clientConnected || !g_srv) {
        PostLog("!!", "no client connected, cannot send");
        return;
    }
    if (g_srv->SendLine(s)) {
        PostLog(">>", s);
    } else {
        PostLog("!!", "send failed");
    }
    SetWindowTextW(g_hInput, L"");
    SetFocus(g_hInput);
}

static void Layout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;

    int statusH = 28;
    int rowH = 28;
    int pad = 8;
    int btnW = 100;

    // status
    MoveWindow(g_hStatus, pad, pad, W - 2*pad, statusH, TRUE);
    // input row at bottom
    int inputY = H - pad - rowH;
    MoveWindow(g_hInput, pad, inputY, W - 3*pad - btnW, rowH, TRUE);
    MoveWindow(g_hBtnSend, W - pad - btnW, inputY, btnW, rowH, TRUE);
    // list fills the middle
    int listY = pad + statusH + pad;
    int listH = inputY - pad - listY;
    MoveWindow(g_hList, pad, listY, W - 2*pad, listH, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hStatus  = CreateWindowW(L"STATIC", L"Initializing...",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 0, 0, hwnd, (HMENU)1001, nullptr, nullptr);
        g_hList    = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT | LBS_NOTIFY,
            0, 0, 0, 0, hwnd, (HMENU)1002, nullptr, nullptr);
        g_hInput   = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
            0, 0, 0, 0, hwnd, (HMENU)1003, nullptr, nullptr);
        g_hBtnSend = CreateWindowW(L"BUTTON", L"Send",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_DISABLED,
            0, 0, 0, 0, hwnd, (HMENU)1004, nullptr, nullptr);

        HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)hf, TRUE);
        SendMessageW(g_hInput,  WM_SETFONT, (WPARAM)hf, TRUE);
        SendMessageW(g_hBtnSend,WM_SETFONT, (WPARAM)hf, TRUE);
        // Monospace for list.
        HFONT mono = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, FF_MODERN | FIXED_PITCH, L"Consolas");
        SendMessageW(g_hList, WM_SETFONT, (WPARAM)mono, TRUE);
        return 0;
    }
    case WM_SIZE:
        Layout(hwnd);
        return 0;

    case WM_APP_LOG: {
        char* msg = (char*)lp;
        if (msg) {
            AppendListLine(Utf8ToWide(msg));
            free(msg);
        }
        return 0;
    }
    case WM_APP_STATUS: {
        char* s = (char*)lp;
        if (s) {
            std::wstring w = L"status: " + Utf8ToWide(s);
            SetWindowTextW(g_hStatus, w.c_str());
            free(s);
        }
        return 0;
    }
    case WM_APP + 10:
        EnableWindow(g_hBtnSend, TRUE);
        SetFocus(g_hInput);
        return 0;
    case WM_APP + 11:
        EnableWindow(g_hBtnSend, FALSE);
        return 0;

    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        WORD code = HIWORD(wp);
        if (id == 1004 && code == BN_CLICKED) { DoSend(); return 0; }
        if (id == 1003 && code == EN_CHANGE)  { /* could enable on text */ return 0; }
        break;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_srv) { g_srv->Close(); }
        if (g_go)  { g_go->Stop(); }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    try {
        winrt::init_apartment();
    } catch (winrt::hresult_error const& e) {
        MessageBoxW(nullptr, e.message().c_str(), L"init_apartment failed", MB_ICONERROR);
        return 2;
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"wifip2p_main";
    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"RegisterClass failed", L"Error", MB_ICONERROR);
        return 3;
    }

    g_hwnd = CreateWindowExW(0, L"wifip2p_main", L"wifip2p Windows GO  (Stage 1)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 540,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 3;

    ShowWindow(g_hwnd, nShow);
    UpdateWindow(g_hwnd);
    Layout(g_hwnd);

    // Subclass input to handle Enter -> Send.
    SetWindowSubclass(g_hInput,
        [](HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) -> LRESULT {
            if (m == WM_KEYDOWN && w == VK_RETURN) { DoSend(); return 0; }
            if (m == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
            return DefSubclassProc(h, m, w, l);
        }, 1, 0);

    g_worker = std::thread(WorkerMain);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        if (IsDialogMessageW(g_hwnd, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    if (g_worker.joinable()) g_worker.join();
    delete g_srv; g_srv = nullptr;
    delete g_go;  g_go  = nullptr;
    return 0;
}
