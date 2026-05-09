cmake_minimum_required(VERSION 3.20)
project(wifip2p_windows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W3 /utf-8 /await /EHsc)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
        UNICODE _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
endif()

add_executable(wifip2p_windows WIN32
    src/main.cpp
    src/wifi_direct_go.cpp
    src/tcp_server.cpp
)

target_include_directories(wifip2p_windows PRIVATE src)

# C++/WinRT comes with Windows SDK 10.0.17134+; no NuGet needed.
target_link_libraries(wifip2p_windows PRIVATE
    windowsapp     # WinRT runtime
    ws2_32         # TCP sockets
    runtimeobject  # RoInitialize
    comctl32       # ListBox / common controls
    comdlg32
)
