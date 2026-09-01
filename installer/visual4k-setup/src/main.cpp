// Visual4k-Setup -- one double-click from a downloaded zip to a working
// supersampled desktop.
//
// This exists because every earlier route asked too much. A PowerShell script
// is refused outright by the default execution policy; creating the virtual
// display device meant either the WDK's devcon or six steps through Device
// Manager's legacy hardware wizard; and the display rearrangement at the end
// is the one step where a wrong move leaves the user looking at a dark panel
// with no way back. Each of those is handled here.
//
// The order of the last few steps is deliberate and is the whole safety story:
// the compositor is started while the virtual display is still secondary, so
// that by the time the desktop moves onto it there is already something
// drawing it onto the physical panel. Nothing here is left to a Windows
// timeout -- ChangeDisplaySettingsEx does not have one -- so the countdown and
// the revert are ours.

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "CertTrust.h"
#include "DeviceInstall.h"
#include "DisplayConfig.h"
#include "TestSigning.h"
#include "Ui.h"

namespace visual4k {
namespace {

constexpr uint32_t kVirtualWidth = 3840;
constexpr uint32_t kVirtualHeight = 2160;

// How long Windows may take to enumerate the new display after the driver
// binds. Generous: the failure it guards against looks like "the driver
// installed but nothing happened", which is expensive to debug.
constexpr int kDisplayAppearSeconds = 20;

// Long enough for the compositor to open its window, compile the shaders and
// present, so that its exit code is meaningful if it did not.
constexpr int kCompositorSettleMs = 4000;

struct Paths {
    std::wstring root;
    std::wstring inf;
    std::wstring cer;
    std::wstring host;
};

std::wstring ExecutableDirectory()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                                                static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return std::wstring();
        if (length < buffer.size() - 1)
            break;
        buffer.resize(buffer.size() * 2);
    }
    std::wstring path(buffer.data());
    const size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool Exists(const std::wstring& path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ResolvePaths(Paths* paths)
{
    paths->root = ExecutableDirectory();
    if (paths->root.empty()) {
        Line(L"Could not work out where this program is running from.", Tone::Bad);
        return false;
    }

    paths->inf = paths->root + L"\\driver\\Visual4kDisplay.inf";
    paths->host = paths->root + L"\\visual4k-host.exe";

    if (!Exists(paths->inf)) {
        Line(L"The driver folder is missing.", Tone::Bad);
        Line(L"  expected: " + paths->inf);
        Blank();
        Line(L"This usually means the zip was opened rather than extracted, so");
        Line(L"only this one file was copied out of it. Extract the whole zip");
        Line(L"to a real folder and run the program from there.");
        return false;
    }
    if (!Exists(paths->host)) {
        Line(L"visual4k-host.exe is missing from " + paths->root, Tone::Bad);
        Line(L"Extract the whole zip, not just part of it.");
        return false;
    }

    // The certificate's name carries the build, so it is found rather than
    // assumed.
    WIN32_FIND_DATAW found = {};
    const std::wstring pattern = paths->root + L"\\driver\\*.cer";
    HANDLE search = FindFirstFileW(pattern.c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        paths->cer = paths->root + L"\\driver\\" + found.cFileName;
        FindClose(search);
    }
    return true;
}

bool RunningElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation = {};
    DWORD size = sizeof(elevation);
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation,
                                        sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

unsigned long WindowsBuild()
{
    // The documented version APIs lie to a process without the matching
    // manifest entry, so this reads the number Windows records for itself.
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
        return 0;

    wchar_t value[32] = {};
    DWORD size = sizeof(value);
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, L"CurrentBuildNumber", nullptr,
                                         &type, reinterpret_cast<LPBYTE>(value),
                                         &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS)
        return 0;
    return wcstoul(value, nullptr, 10);
}

// Waits for the virtual display to be enumerated after the driver binds.
bool WaitForVirtualDisplay(DisplayInfo* out, int seconds,
                           const std::vector<std::wstring>& knownBefore)
{
    for (int i = 0; i < seconds * 4; ++i) {
        if (FindVirtualDisplay(out, knownBefore))
            return true;
        Sleep(250);
    }
    return false;
}

// Last resort when the display cannot be identified automatically.
//
// Better than failing: a display that exists but was not recognised is a
// naming problem, and a person looking at their own monitors can resolve it in
// one keystroke where the program cannot.
bool AskWhichDisplay(DisplayInfo* out)
{
    std::vector<DisplayInfo> attached;
    for (const auto& display : EnumerateDisplays()) {
        if (display.attached)
            attached.push_back(display);
    }
    if (attached.empty())
        return false;

    Blank();
    Line(L"The driver installed, but setup could not tell which of these is the",
         Tone::Warn);
    Line(L"virtual display. These are attached right now:", Tone::Warn);
    Blank();
    for (size_t i = 0; i < attached.size(); ++i) {
        Line(L"  " + std::to_wstring(i + 1) + L"  " + attached[i].deviceName +
             L"  " + std::to_wstring(attached[i].width) + L"x" +
             std::to_wstring(attached[i].height) + L"  " +
             attached[i].description);
    }
    Blank();
    Line(L"The virtual one is the display that was not there a minute ago. It is");
    Line(L"not one of your monitors, so it is the entry you do not recognise.");
    Blank();
    std::fputws(L"Which number? (Enter to give up): ", stdout);
    std::fflush(stdout);

    wchar_t buffer[16] = {};
    if (std::fgetws(buffer, 16, stdin) == nullptr)
        return false;

    const long choice = wcstol(buffer, nullptr, 10);
    if (choice < 1 || static_cast<size_t>(choice) > attached.size())
        return false;

    *out = attached[static_cast<size_t>(choice) - 1];
    return true;
}

HANDLE StartCompositor(const Paths& paths, const std::wstring& sourceName,
                       const std::wstring& outputName)
{
    std::wstring commandLine = L"\"" + paths.host + L"\"" +
                               L" --source " + sourceName +
                               L" --output " + outputName;

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};

    // The working directory has to be the program's own folder: the compositor
    // loads its shaders from a relative path, and setup may have been started
    // from anywhere.
    if (!CreateProcessW(nullptr, &commandLine[0], nullptr, nullptr, FALSE, 0,
                        nullptr, paths.root.c_str(), &si, &pi)) {
        Line(L"  could not start the compositor: " +
                 DescribeError(GetLastError()), Tone::Bad);
        return nullptr;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

// Stops any compositor left running from an earlier session.
//
// Removal has to do this: the compositor's window is borderless and topmost on
// the panel, so leaving it up while its source display is deleted covers the
// screen with a frozen image of a desktop that no longer exists.
int StopRunningCompositors()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    int stopped = 0;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"visual4k-host.exe") != 0)
                continue;
            HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE,
                                         entry.th32ProcessID);
            if (process == nullptr)
                continue;
            if (TerminateProcess(process, 0))
                ++stopped;
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return stopped;
}

bool StillRunning(HANDLE process)
{
    return WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

void StopCompositor(HANDLE process)
{
    if (process == nullptr)
        return;
    if (StillRunning(process))
        TerminateProcess(process, 0);
    CloseHandle(process);
}

// --- the steps -------------------------------------------------------------

bool StepPreflight(const Paths& paths)
{
    Heading(L"Checking this machine");

    if (!RunningElevated()) {
        Line(L"This program needs administrator rights and does not have them.",
             Tone::Bad);
        Line(L"Right-click it and choose \"Run as administrator\".");
        return false;
    }
    Line(L"  running as administrator");

    const unsigned long build = WindowsBuild();
    if (build != 0 && build < 16299) {
        Line(L"  Windows build " + std::to_wstring(build) +
                 L" is too old; the indirect display driver needs 16299 or later.",
             Tone::Bad);
        return false;
    }
    Line(L"  Windows build " + std::to_wstring(build));
    Line(L"  files found in " + paths.root);
    return true;
}

// Returns false when setup should stop -- either because it failed, or because
// the machine has to restart before it can continue.
bool StepTestSigning()
{
    Heading(L"Driver signature enforcement");

    const TestSigningState state = QueryTestSigning();
    if (state == TestSigningState::On) {
        Line(L"  test signing is on", Tone::Good);
        return true;
    }
    if (state == TestSigningState::Unknown) {
        Line(L"  could not read the current state; continuing anyway.", Tone::Warn);
        Line(L"  If the driver install fails with a signature error, that is why.");
        return true;
    }

    Line(L"Windows will not load this driver as things stand.", Tone::Warn);
    Blank();
    Line(L"The driver is signed, but by a certificate the build made for");
    Line(L"itself rather than by a public authority. Windows accepts such a");
    Line(L"signature only in test signing mode, which is a boot setting.");
    Blank();
    Line(L"What that costs you, stated plainly:", Tone::Warn);
    Line(L"  - While it is on, Windows will load ANY driver signed by a");
    Line(L"    certificate trusted on this machine, not only this one.");
    Line(L"  - A \"Test Mode\" watermark sits in the corner of the screen.");
    Line(L"  - It survives restarts until you turn it off. This program's");
    Line(L"    Remove option turns it back off for you.");
    Blank();

    if (!Confirm(L"Turn test signing on?")) {
        Line(L"Nothing was changed.", Tone::Dim);
        return false;
    }
    if (!EnableTestSigning())
        return false;

    Line(L"  test signing will be on after the next restart", Tone::Good);
    Blank();
    Line(L"Restart, then run this program again and it will carry on from here.",
         Tone::Warn);
    Blank();

    if (Confirm(L"Restart now?")) {
        if (RebootNow())
            Line(L"Restarting in 10 seconds.", Tone::Good);
    }
    return false;
}

bool StepInstallDriver(const Paths& paths)
{
    Heading(L"Installing the virtual display");

    if (!paths.cer.empty()) {
        const Result trusted = TrustCertificate(paths.cer);
        if (!trusted.ok) {
            Line(L"  " + trusted.detail, Tone::Bad);
            return false;
        }
    } else {
        Line(L"  no certificate in the driver folder; assuming the package is",
             Tone::Warn);
        Line(L"  signed by something this machine already trusts.", Tone::Warn);
    }

    bool rebootRequired = false;
    const Result installed = InstallVirtualDisplay(paths.inf, &rebootRequired);
    if (!installed.ok) {
        Line(L"  " + installed.detail, Tone::Bad);
        return false;
    }
    if (rebootRequired) {
        Line(L"  Windows wants a restart to finish this. Restart and run setup",
             Tone::Warn);
        Line(L"  again.", Tone::Warn);
        return false;
    }
    return true;
}

// Finds the virtual display, or explains why there is not one.
//
// The explanation comes before the question. An earlier version offered the
// list of attached displays first, so when the device had failed to start it
// presented the reader's own two monitors and asked which was the one that
// was not there. Windows had recorded the actual reason the whole time.
bool FindVirtualDisplayOrExplain(DisplayInfo* out,
                                 const std::vector<std::wstring>& displaysBefore)
{
    if (WaitForVirtualDisplay(out, kDisplayAppearSeconds, displaysBefore))
        return true;

    const DeviceStatus status = QueryVirtualDisplayStatus();

    // Only worth asking when the driver is actually running: then a display
    // does exist and it is the identification that failed, which is the one
    // case a person can settle and the program cannot.
    if (status.present && status.started && AskWhichDisplay(out))
        return true;

    Blank();
    Line(L"No virtual display appeared.", Tone::Bad);
    Blank();

    if (!status.present) {
        Line(L"The device is not there at all, which means the install did not "
             L"take.", Tone::Bad);
        Line(L"Try option 3 to remove what exists, then option 1 again.");
    } else if (status.started) {
        Line(L"The device is present and running, so the driver started and "
             L"only the", Tone::Warn);
        Line(L"display did not come up. That is worth reporting as a bug.",
             Tone::Warn);
    } else {
        Line(L"Windows reports the device as present but stopped:", Tone::Bad);
        Line(L"  problem code " + std::to_wstring(status.problemCode), Tone::Bad);
        if (!status.explanation.empty())
            Line(L"  " + status.explanation);
    }
    return false;
}

bool StepArrangeDisplays(const Paths& paths,
                        const std::vector<std::wstring>& displaysBefore)
{
    Heading(L"Setting up the displays");

    DisplayInfo virtualDisplay;
    if (!FindVirtualDisplayOrExplain(&virtualDisplay, displaysBefore))
        return false;

    Line(L"  virtual display: " + virtualDisplay.deviceName + L" (" +
         virtualDisplay.description + L")", Tone::Good);

    if (!SetResolution(virtualDisplay.deviceName, kVirtualWidth, kVirtualHeight)) {
        Line(L"  could not set it to 3840x2160.", Tone::Bad);
        return false;
    }
    Line(L"  set to 3840x2160");

    DisplayInfo panel;
    if (!FindPresentationPanel(virtualDisplay.deviceName, &panel)) {
        Line(L"  no other display to show the result on.", Tone::Bad);
        return false;
    }
    Line(L"  drawing onto: " + panel.deviceName + L" (" +
         std::to_wstring(panel.width) + L"x" + std::to_wstring(panel.height) +
         L")");

    // Started before the desktop moves, so that the panel is already showing
    // the virtual display by the time the virtual display is where everything
    // lives. Doing this the other way round is the difference between a
    // configuration step and being locked out of your own machine.
    Blank();
    Line(L"Starting the compositor before moving the desktop, so the panel is");
    Line(L"never showing nothing.", Tone::Dim);

    HANDLE compositor = StartCompositor(paths, virtualDisplay.deviceName,
                                        panel.deviceName);
    if (compositor == nullptr)
        return false;

    Sleep(kCompositorSettleMs);
    if (!StillRunning(compositor)) {
        DWORD exitCode = 0;
        GetExitCodeProcess(compositor, &exitCode);
        CloseHandle(compositor);
        Line(L"  the compositor stopped immediately (exit code " +
                 std::to_wstring(exitCode) + L").", Tone::Bad);
        Line(L"  Not moving the desktop; you would have been left with a dark");
        Line(L"  panel. Run visual4k-host.exe from a command prompt to see what");
        Line(L"  it says.");
        return false;
    }
    Line(L"  compositor running", Tone::Good);

    const SavedLayout before = CaptureLayout();

    if (!MakePrimary(virtualDisplay.deviceName)) {
        Line(L"  could not make the virtual display primary.", Tone::Bad);
        RestoreLayout(before);
        StopCompositor(compositor);
        return false;
    }

    Blank();
    if (!ConfirmWithinSeconds(
            L"The desktop is now on the virtual 4K display, and the panel is "
            L"showing it.\nIf you can read this, it worked.", 20)) {
        Line(L"  no confirmation -- putting the displays back.", Tone::Warn);
        RestoreLayout(before);
        StopCompositor(compositor);
        Line(L"  reverted. Nothing else was undone; run setup again to retry.");
        return false;
    }

    // Only the handle is released. The compositor keeps running -- it has to
    // outlive setup, because it is the only thing drawing the desktop onto the
    // panel from here on.
    CloseHandle(compositor);
    return true;
}

int DoInstall(const Paths& paths)
{
    if (!StepPreflight(paths))
        return 1;
    if (!StepTestSigning())
        return 1;

    // Recorded before the device exists, so the new display can be identified
    // by being new rather than by being named what we expect.
    const std::vector<std::wstring> displaysBefore = AttachedDisplayNames();

    if (!StepInstallDriver(paths))
        return 1;
    if (!StepArrangeDisplays(paths, displaysBefore))
        return 1;

    Heading(L"Done");
    Line(L"Everything is running.", Tone::Good);
    Blank();
    Line(L"  Quit the compositor with Ctrl+Alt+F12, from anywhere.");
    Line(L"  Start it again by running this program and choosing option 2.");
    Line(L"  Undo all of it, including test signing, with option 3.");
    Blank();
    Line(L"The panel is showing a 3840x2160 desktop resolved down to its own");
    Line(L"resolution. Text and edges carry detail a 1440p desktop cannot,");
    Line(L"because they were drawn at 4K before being resolved.", Tone::Dim);
    return 0;
}

int DoRunCompositor(const Paths& paths)
{
    Heading(L"Starting the compositor");

    DisplayInfo virtualDisplay;
    if (!FindVirtualDisplay(&virtualDisplay)) {
        Line(L"No virtual display found. Install it first (option 1).", Tone::Bad);
        return 1;
    }
    DisplayInfo panel;
    if (!FindPresentationPanel(virtualDisplay.deviceName, &panel)) {
        Line(L"No other display to draw on.", Tone::Bad);
        return 1;
    }

    HANDLE compositor = StartCompositor(paths, virtualDisplay.deviceName,
                                        panel.deviceName);
    if (compositor == nullptr)
        return 1;

    Sleep(kCompositorSettleMs);
    if (!StillRunning(compositor)) {
        DWORD exitCode = 0;
        GetExitCodeProcess(compositor, &exitCode);
        CloseHandle(compositor);
        Line(L"  it stopped immediately (exit code " +
                 std::to_wstring(exitCode) + L").", Tone::Bad);
        return 1;
    }
    CloseHandle(compositor);

    Line(L"  running; source " + virtualDisplay.deviceName + L", output " +
         panel.deviceName, Tone::Good);
    Line(L"  Ctrl+Alt+F12 quits it.");
    return 0;
}

int DoRemove(const Paths& paths)
{
    Heading(L"Removing Visual-4k");

    if (!RunningElevated()) {
        Line(L"This needs administrator rights.", Tone::Bad);
        return 1;
    }

    const int stopped = StopRunningCompositors();
    if (stopped > 0)
        Line(L"  stopped " + std::to_wstring(stopped) + L" running compositor(s)");

    // The desktop has to come off the virtual display before the display goes
    // away, or Windows picks the replacement primary itself and the panel is
    // left showing whatever was last presented to it.
    DisplayInfo virtualDisplay;
    if (FindVirtualDisplay(&virtualDisplay) && virtualDisplay.primary) {
        DisplayInfo panel;
        if (FindPresentationPanel(virtualDisplay.deviceName, &panel)) {
            Line(L"  moving the desktop back to " + panel.deviceName);
            MakePrimary(panel.deviceName);
            // Give the compositor a moment to notice its source went primary
            // elsewhere before the device disappears underneath it.
            Sleep(1000);
        }
    }

    bool rebootRequired = false;
    const Result removed = RemoveVirtualDisplay(paths.inf, &rebootRequired);
    if (!removed.ok)
        Line(L"  " + removed.detail, Tone::Bad);

    if (!paths.cer.empty())
        UntrustCertificate(paths.cer);

    Blank();
    if (QueryTestSigning() == TestSigningState::On) {
        Line(L"Test signing is still on, and it lowers this machine's security",
             Tone::Warn);
        Line(L"for as long as it stays on.", Tone::Warn);
        if (Confirm(L"Turn test signing off?")) {
            if (DisableTestSigning()) {
                Line(L"  off after the next restart", Tone::Good);
                rebootRequired = true;
            }
        }
    }

    Blank();
    if (rebootRequired)
        Line(L"Restart to finish removing it.", Tone::Warn);
    else
        Line(L"Removed.", Tone::Good);
    return 0;
}

int Menu(const Paths& paths)
{
    Line(L"  1  Install Visual-4k and set it up");
    Line(L"  2  Start the compositor (already installed)");
    Line(L"  3  Remove Visual-4k");
    Line(L"  4  Quit");
    Blank();
    std::fputws(L"Choose 1-4: ", stdout);
    std::fflush(stdout);

    wchar_t buffer[16] = {};
    if (std::fgetws(buffer, 16, stdin) == nullptr)
        return 0;

    switch (buffer[0]) {
        case L'1': return DoInstall(paths);
        case L'2': return DoRunCompositor(paths);
        case L'3': return DoRemove(paths);
        default:   return 0;
    }
}

}  // namespace
}  // namespace visual4k

int wmain(int argc, wchar_t** argv)
{
    using namespace visual4k;

    SetConsoleTitleW(L"Visual-4k Setup");

    Line(L"Visual-4k Setup", Tone::Good);
    Line(L"Renders your desktop at 3840x2160 on a virtual display and resolves",
         Tone::Dim);
    Line(L"it onto your real panel, so text and edges carry detail the panel's",
         Tone::Dim);
    Line(L"own resolution cannot draw directly.", Tone::Dim);
    Blank();

    Paths paths;
    if (!ResolvePaths(&paths)) {
        PressEnterToClose();
        return 1;
    }

    int status = 0;
    if (argc > 1) {
        const std::wstring command = argv[1];
        if (command == L"--install")        status = DoInstall(paths);
        else if (command == L"--run")       status = DoRunCompositor(paths);
        else if (command == L"--remove")    status = DoRemove(paths);
        else {
            Line(L"Usage: Visual4k-Setup.exe [--install|--run|--remove]", Tone::Bad);
            status = 2;
        }
    } else {
        status = Menu(paths);
    }

    PressEnterToClose();
    return status;
}
