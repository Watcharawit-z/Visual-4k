// Console presentation for the setup program.
//
// The setup runs by double-click, so its console is the only thing the user
// ever sees: there is no log to go back to and no terminal scrollback that
// outlives it. Every step therefore says what it is about to do before doing
// it, and every failure says what to try next rather than only what broke.

#pragma once

#include <string>

namespace visual4k {

enum class Tone { Plain, Good, Warn, Bad, Dim };

void Line(const std::wstring& text, Tone tone = Tone::Plain);
void Heading(const std::wstring& text);
void Blank();

// Formats a Win32 error the way the user needs it: the number for searching,
// and the system's own message for reading.
std::wstring DescribeError(unsigned long code);

// Yes/no, defaulting to no. Anything but y/Y is no, including a bare Enter,
// because every prompt in this program guards something that changes the
// machine.
bool Confirm(const std::wstring& question);

// Waits for Enter for up to `seconds`, showing the time left.
//
// This is the safety net for the display switch. Windows reverts a display
// change made from the Settings app if you do not confirm it, but that is a
// feature of that app, not of the API this program calls -- a change made
// here is permanent the moment it is applied. So the countdown has to be
// ours, and the revert has to be ours too.
//
// Returns true if Enter arrived in time.
bool ConfirmWithinSeconds(const std::wstring& question, int seconds);

// Holds the window open so a double-clicked run can be read.
void PressEnterToClose();

}  // namespace visual4k
