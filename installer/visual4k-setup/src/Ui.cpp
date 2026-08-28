#include "Ui.h"

#include <windows.h>

#include <cstdio>
#include <cwchar>

namespace visual4k {
namespace {

WORD AttributeFor(Tone tone)
{
    switch (tone) {
        case Tone::Good:
            return static_cast<WORD>(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
        case Tone::Warn:
            return static_cast<WORD>(FOREGROUND_RED | FOREGROUND_GREEN |
                                     FOREGROUND_INTENSITY);
        case Tone::Bad:
            return static_cast<WORD>(FOREGROUND_RED | FOREGROUND_INTENSITY);
        case Tone::Dim:
            return static_cast<WORD>(FOREGROUND_BLUE | FOREGROUND_GREEN |
                                     FOREGROUND_RED);
        case Tone::Plain:
        default:
            return static_cast<WORD>(FOREGROUND_BLUE | FOREGROUND_GREEN |
                                     FOREGROUND_RED | FOREGROUND_INTENSITY);
    }
}

void WriteColoured(const std::wstring& text, Tone tone)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info = {};
    const bool haveConsole = GetConsoleScreenBufferInfo(out, &info) != FALSE;

    if (haveConsole)
        SetConsoleTextAttribute(out, AttributeFor(tone));

    std::fputws(text.c_str(), stdout);

    if (haveConsole)
        SetConsoleTextAttribute(out, info.wAttributes);
}

}  // namespace

void Line(const std::wstring& text, Tone tone)
{
    WriteColoured(text, tone);
    std::fputws(L"\n", stdout);
    std::fflush(stdout);
}

void Heading(const std::wstring& text)
{
    std::fputws(L"\n", stdout);
    WriteColoured(L"== " + text, Tone::Good);
    std::fputws(L"\n", stdout);
    std::fflush(stdout);
}

void Blank()
{
    std::fputws(L"\n", stdout);
    std::fflush(stdout);
}

std::wstring DescribeError(unsigned long code)
{
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring text;
    wchar_t number[32];
    std::swprintf(number, 32, L"0x%08lX", code);
    text = number;

    if (length != 0 && buffer != nullptr) {
        std::wstring message(buffer, length);
        // FormatMessage ends its text with CRLF, which would break the line
        // this is being pasted into.
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n' ||
                message.back() == L' '))
            message.pop_back();
        if (!message.empty())
            text += L" (" + message + L")";
    }
    if (buffer != nullptr)
        LocalFree(buffer);
    return text;
}

bool Confirm(const std::wstring& question)
{
    WriteColoured(question + L" [y/N] ", Tone::Warn);
    std::fflush(stdout);

    wchar_t buffer[16] = {};
    if (std::fgetws(buffer, 16, stdin) == nullptr)
        return false;
    return buffer[0] == L'y' || buffer[0] == L'Y';
}

bool ConfirmWithinSeconds(const std::wstring& question, int seconds)
{
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);

    // The countdown is redrawn in place, so it has to be the only thing on its
    // line; the question goes above it.
    Line(question, Tone::Warn);

    for (int remaining = seconds; remaining > 0; --remaining) {
        wchar_t status[96];
        std::swprintf(status, 96,
                      L"\r  reverting in %2d seconds -- press Enter to keep  ",
                      remaining);
        WriteColoured(status, Tone::Warn);
        std::fflush(stdout);

        const DWORD deadline = GetTickCount() + 1000;
        for (;;) {
            const DWORD now = GetTickCount();
            // Unsigned subtraction, so this stays correct across the 49-day
            // wrap of GetTickCount rather than spinning for a month.
            if (static_cast<DWORD>(deadline - now) > 1000u)
                break;

            if (WaitForSingleObject(input, 100) != WAIT_OBJECT_0)
                continue;

            INPUT_RECORD record = {};
            DWORD read = 0;
            if (!ReadConsoleInputW(input, &record, 1, &read) || read == 0)
                continue;
            // Only a key going down counts: the Enter that started this
            // program is still in the buffer as a key-up otherwise.
            if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown &&
                record.Event.KeyEvent.wVirtualKeyCode == VK_RETURN) {
                std::fputws(L"\n", stdout);
                return true;
            }
        }
    }

    std::fputws(L"\n", stdout);
    return false;
}

void PressEnterToClose()
{
    Blank();
    WriteColoured(L"Press Enter to close this window.", Tone::Dim);
    std::fflush(stdout);
    wchar_t buffer[16];
    (void)std::fgetws(buffer, 16, stdin);
    std::fputws(L"\n", stdout);
}

}  // namespace visual4k
