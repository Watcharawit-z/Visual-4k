#include "Diagnostics.h"

#include <cwchar>

namespace visual4k {

void RecordStage(const wchar_t* stage, StatusCode status)
{
    wchar_t line[256];
    std::swprintf(line, 256, L"Visual4kDisplay: %ls -> 0x%08lX\n", stage,
                  static_cast<unsigned long>(status));
    OutputDebugStringW(line);

    // Best effort from here down. A driver host without write access to the
    // key simply leaves no record, which is worth nothing but costs nothing;
    // failing the driver because its logging failed would be worse than the
    // problem being logged.
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kDiagnosticsKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return;

    RegSetValueExW(key, L"LastStage", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(stage),
                   static_cast<DWORD>((wcslen(stage) + 1) * sizeof(wchar_t)));

    const DWORD statusValue = static_cast<DWORD>(status);
    RegSetValueExW(key, L"LastStatus", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&statusValue),
                   sizeof(statusValue));

    SYSTEMTIME now = {};
    GetSystemTime(&now);
    wchar_t when[64];
    std::swprintf(when, 64, L"%04u-%02u-%02u %02u:%02u:%02uZ", now.wYear,
                  now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    RegSetValueExW(key, L"LastTime", 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(when),
                   static_cast<DWORD>((wcslen(when) + 1) * sizeof(wchar_t)));

    RegCloseKey(key);
}

}  // namespace visual4k
