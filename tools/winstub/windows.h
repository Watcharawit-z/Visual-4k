// Minimal Windows declarations -- see tools/winstub/README.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

using BYTE = unsigned char;
using WORD = unsigned short;
using DWORD = unsigned long;
using UINT = unsigned int;
using INT = int;
using LONG = long;
using ULONG = unsigned long;
using BOOL = int;
using UINT64 = unsigned long long;
using LONGLONG = long long;
using SIZE_T = std::size_t;
using HRESULT = long;
using LPVOID = void*;
using LPCVOID = const void*;
using PVOID = void*;
using LPSTR = char*;
using LPCSTR = const char*;
using WCHAR = wchar_t;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;
using UINT_PTR = std::uintptr_t;
using LONG_PTR = std::intptr_t;
using WPARAM = UINT_PTR;
using LPARAM = LONG_PTR;
using LRESULT = LONG_PTR;
using ATOM = WORD;

struct HWND__ { int unused; };  using HWND = HWND__*;
struct HINSTANCE__ { int unused; }; using HINSTANCE = HINSTANCE__*;
using HMODULE = HINSTANCE;
struct HICON__ { int unused; }; using HICON = HICON__*;
using HCURSOR = HICON;
struct HBRUSH__ { int unused; }; using HBRUSH = HBRUSH__*;
struct HMENU__ { int unused; }; using HMENU = HMENU__*;
using HANDLE = void*;

#define TRUE 1
#define FALSE 0
#define WINAPI
#define CALLBACK
#define STDMETHODCALLTYPE
#define MAX_PATH 260
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))

#define S_OK             ((HRESULT)0L)
#define S_FALSE          ((HRESULT)1L)
#define E_FAIL           ((HRESULT)0x80004005L)
#define E_INVALIDARG     ((HRESULT)0x80070057L)
#define E_OUTOFMEMORY    ((HRESULT)0x8007000EL)
#define E_NOINTERFACE    ((HRESULT)0x80004002L)
#define E_NOT_VALID_STATE ((HRESULT)0x8007139FL)

inline bool FAILED(HRESULT hr) { return hr < 0; }
inline bool SUCCEEDED(HRESULT hr) { return hr >= 0; }

struct GUID { DWORD Data1; WORD Data2; WORD Data3; BYTE Data4[8]; };
using IID = GUID;
using REFIID = const IID&;
using REFGUID = const GUID&;
struct LUID { DWORD LowPart; LONG HighPart; };

struct POINT { LONG x, y; };
struct RECT { LONG left, top, right, bottom; };
struct SIZE { LONG cx, cy; };

struct MSG {
    HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam;
    DWORD time; POINT pt;
};

using WNDPROC = LRESULT (CALLBACK*)(HWND, UINT, WPARAM, LPARAM);

struct WNDCLASSEXW {
    UINT cbSize; UINT style; WNDPROC lpfnWndProc;
    int cbClsExtra; int cbWndExtra; HINSTANCE hInstance;
    HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground;
    LPCWSTR lpszMenuName; LPCWSTR lpszClassName; HICON hIconSm;
};

#define WM_CLOSE   0x0010
#define WM_DESTROY 0x0002
#define WM_KEYDOWN 0x0100
#define WM_HOTKEY  0x0312
#define MOD_ALT 0x0001
#define MOD_CONTROL 0x0002
#define MOD_SHIFT 0x0004
#define MOD_NOREPEAT 0x4000
#define VK_F12 0x7B
#define VK_ESCAPE  0x1B
#define PM_REMOVE  0x0001
#define WS_POPUP   0x80000000L
#define WS_VISIBLE 0x10000000L
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_NOREDIRECTIONBITMAP 0x00200000L
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define IDC_ARROW ((LPCWSTR)(UINT_PTR)32512)
#define WAIT_OBJECT_0 0x00000000L
#define WAIT_TIMEOUT  0x00000102L
#define INFINITE 0xFFFFFFFF

using DPI_AWARENESS_CONTEXT = void*;
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)

extern "C" {
ATOM RegisterClassExW(const WNDCLASSEXW*);
HWND CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int,
                     HWND, HMENU, HINSTANCE, LPVOID);
LRESULT DefWindowProcW(HWND, UINT, WPARAM, LPARAM);
BOOL PeekMessageW(MSG*, HWND, UINT, UINT, UINT);
BOOL TranslateMessage(const MSG*);
LRESULT DispatchMessageW(const MSG*);
int GetSystemMetrics(int);
HMODULE GetModuleHandleW(LPCWSTR);
HCURSOR LoadCursorW(HINSTANCE, LPCWSTR);
void Sleep(DWORD);
BOOL SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT);
void OutputDebugStringA(LPCSTR);
BOOL RegisterHotKey(HWND, int, UINT, UINT);
BOOL UnregisterHotKey(HWND, int);
HANDLE CreateEventW(void*, BOOL, BOOL, LPCWSTR);
BOOL SetEvent(HANDLE);
BOOL CloseHandle(HANDLE);
DWORD WaitForSingleObject(HANDLE, DWORD);
DWORD WaitForMultipleObjects(DWORD, const HANDLE*, BOOL, DWORD);
}

// COM base, enough for ComPtr and the interfaces we touch.
struct IUnknown {
    virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) = 0;
    virtual ULONG STDMETHODCALLTYPE AddRef() = 0;
    virtual ULONG STDMETHODCALLTYPE Release() = 0;
};

template <typename T> struct StubIidOf { static const IID value; };
template <typename T> const IID StubIidOf<T>::value = {};
template <typename T> const IID& __uuidof_helper() { return StubIidOf<T>::value; }

#define IID_PPV_ARGS(pp) __uuidof_helper<std::remove_pointer_t<std::remove_reference_t<decltype(*(pp))>>>(), \
                         reinterpret_cast<void**>(pp)

#include <type_traits>
