#include "Diagnostics.h"

#include <cwchar>

namespace visual4k {

namespace {

// Opens the diagnostics key for writing, or returns null. Best effort by
// design: a driver host without access simply leaves no record, and failing
// the driver because its logging failed would be worse than the problem being
// logged.
HKEY OpenDiagnosticsKey()
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, kDiagnosticsKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key,
                        nullptr) != ERROR_SUCCESS)
        return nullptr;
    return key;
}

void WriteString(HKEY key, const wchar_t* name, const wchar_t* value)
{
    RegSetValueExW(key, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value),
                   static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
}

}  // namespace

void RecordDetail(const wchar_t* name, const wchar_t* text)
{
    wchar_t line[512];
    std::swprintf(line, 512, L"Visual4kDisplay: %ls = %ls\n", name, text);
    OutputDebugStringW(line);

    HKEY key = OpenDiagnosticsKey();
    if (key == nullptr)
        return;
    WriteString(key, name, text);
    RegCloseKey(key);
}

void RecordStage(const wchar_t* stage, StatusCode status)
{
    wchar_t line[256];
    std::swprintf(line, 256, L"Visual4kDisplay: %ls -> 0x%08lX\n", stage,
                  static_cast<unsigned long>(status));
    OutputDebugStringW(line);

    HKEY key = OpenDiagnosticsKey();
    if (key == nullptr)
        return;

    WriteString(key, L"LastStage", stage);

    const DWORD statusValue = static_cast<DWORD>(status);
    RegSetValueExW(key, L"LastStatus", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&statusValue),
                   sizeof(statusValue));

    SYSTEMTIME now = {};
    GetSystemTime(&now);
    wchar_t when[64];
    std::swprintf(when, 64, L"%04u-%02u-%02u %02u:%02u:%02uZ", now.wYear,
                  now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    WriteString(key, L"LastTime", when);

    RegCloseKey(key);
}

}  // namespace visual4k
