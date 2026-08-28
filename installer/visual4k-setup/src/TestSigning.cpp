#include "TestSigning.h"

#include <windows.h>

#include <string>

#include "Ui.h"

namespace visual4k {
namespace {

// The state is read through the code-integrity query rather than by parsing
// bcdedit's output, because bcdedit prints Yes/No in the system's language and
// a localised machine would report Off for a flag that is on. The numbers here
// come from the code-integrity interface, which is stable but not in the SDK
// headers as a public declaration.
constexpr ULONG kSystemCodeIntegrityInformation = 103;
constexpr ULONG kCodeIntegrityOptionTestSign = 0x02;

struct CodeIntegrityInformation {
    ULONG Length;
    ULONG CodeIntegrityOptions;
};

using NtQuerySystemInformationFn = LONG(WINAPI*)(ULONG, PVOID, ULONG, PULONG);

// Runs a console program to completion with its output suppressed, and reports
// its exit code. Used for bcdedit, which is an executable rather than a script
// and so is unaffected by PowerShell's execution policy -- the whole reason
// this program exists.
bool RunSilently(const std::wstring& commandLine, DWORD* exitCode)
{
    std::wstring mutableCommandLine = commandLine;

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &mutableCommandLine[0], nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != nullptr)
        *exitCode = code;
    return true;
}

bool SetBootFlag(const wchar_t* value)
{
    DWORD exitCode = 1;
    std::wstring command = L"bcdedit.exe /set {current} testsigning ";
    command += value;

    if (!RunSilently(command, &exitCode)) {
        Line(L"  could not run bcdedit: " + DescribeError(GetLastError()),
             Tone::Bad);
        return false;
    }
    if (exitCode != 0) {
        Line(L"  bcdedit refused the change (exit code " +
                 std::to_wstring(exitCode) + L")", Tone::Bad);
        return false;
    }
    return true;
}

}  // namespace

TestSigningState QueryTestSigning()
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
        return TestSigningState::Unknown;

    auto query = reinterpret_cast<NtQuerySystemInformationFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "NtQuerySystemInformation")));
    if (query == nullptr)
        return TestSigningState::Unknown;

    CodeIntegrityInformation info = {};
    info.Length = sizeof(info);
    ULONG returned = 0;
    const LONG status = query(kSystemCodeIntegrityInformation, &info,
                              sizeof(info), &returned);
    if (status < 0)
        return TestSigningState::Unknown;

    return (info.CodeIntegrityOptions & kCodeIntegrityOptionTestSign) != 0
               ? TestSigningState::On
               : TestSigningState::Off;
}

bool EnableTestSigning() { return SetBootFlag(L"on"); }
bool DisableTestSigning() { return SetBootFlag(L"off"); }

bool RebootNow()
{
    // Shutdown needs a privilege that is present but disabled in the token of
    // even an elevated process, so it has to be enabled explicitly first.
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        Line(L"  could not open the process token: " +
                 DescribeError(GetLastError()), Tone::Bad);
        return false;
    }

    TOKEN_PRIVILEGES privileges = {};
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME,
                                    &privileges.Privileges[0].Luid) != FALSE;
    if (ok) {
        AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
        // AdjustTokenPrivileges reports success even when it changed nothing,
        // so the real answer is in GetLastError.
        ok = GetLastError() == ERROR_SUCCESS;
    }
    CloseHandle(token);

    if (!ok) {
        Line(L"  could not acquire shutdown privilege; restart manually.",
             Tone::Bad);
        return false;
    }

    if (!InitiateSystemShutdownExW(nullptr,
                                   const_cast<wchar_t*>(L"Visual-4k setup"),
                                   10, FALSE, TRUE,
                                   SHTDN_REASON_MAJOR_APPLICATION |
                                       SHTDN_REASON_MINOR_INSTALLATION |
                                       SHTDN_REASON_FLAG_PLANNED)) {
        Line(L"  could not start the restart: " + DescribeError(GetLastError()),
             Tone::Bad);
        return false;
    }
    return true;
}

}  // namespace visual4k
