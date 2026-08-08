#pragma once
#include <windows.h>
#include <string>

namespace Persistence {
    bool Install();
    bool Remove();
    bool IsInstalled();
}