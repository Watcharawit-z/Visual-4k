// visual4k-host -- resolves the virtual 4K desktop onto the physical panel.
//
// Usage:
//   visual4k-host [options]
//     --list-displays          enumerate outputs and exit
//     --source <\\.\DISPLAYn>  virtual display to read (default: auto-detect)
//     --kernel <name>          triangle|catrom|mitchell|lanczos2|lanczos3|
//                              lanczos4|gaussian   (default: lanczos2)
//     --sharpness <stops>      RCAS strength; 0 is strongest, -1 disables
//     --denoise                enable RCAS noise attenuation (video/film)
//     --linear                 resolve in linear light (video; see docs)
//     --shaders <dir>          shader directory (default: .\shaders)
//     --vsync <0|1>            default 1
//     --no-cursor              do not composite the mouse pointer
//     --stretch                fill the panel instead of preserving the
//                              source's aspect ratio (letterboxing is the
//                              default; stretching only looks right when the
//                              two aspect ratios already match)
//
// Quit with Ctrl+Alt+F12 from anywhere, or Esc while the window has focus.
// The global hotkey matters more than it sounds: once you click into the
// virtual desktop the compositor loses focus, and Esc stops reaching it.
//
// The window is created borderless and topmost on the physical panel. It is
// deliberately a normal window rather than an exclusive-fullscreen swapchain:
// exclusive mode would take the panel away from DWM and break the very desktop
// we are trying to display.

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <string>
#include <vector>

#include "Duplicator.h"
#include "Renderer.h"

// ComPtr arrives with this: visual4k aliases Microsoft::WRL::ComPtr.
// Naming both here would make every use of it ambiguous.
using namespace visual4k;

namespace {

struct Options {
    std::wstring sourceDisplay;
    std::wstring shaderDir = L"shaders";
    RendererSettings renderer;
    bool listDisplays = false;
    bool vsync = true;
    bool drawCursor = true;
};

// Any value works; it only has to be unique within this process.
constexpr int kQuitHotkeyId = 1;

HWND g_window = nullptr;
bool g_running = true;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
        case WM_CLOSE:
        case WM_DESTROY:
            g_running = false;
            return 0;
        case WM_HOTKEY:
            if (wparam == kQuitHotkeyId) {
                g_running = false;
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                g_running = false;
                return 0;
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// Narrows a wide argument to ASCII, refusing anything outside it.
//
// The obvious std::string(w.begin(), w.end()) truncates each wchar_t to a
// char without complaint, so a mistyped kernel name containing a non-ASCII
// character would arrive as mojibake and be reported as "unknown kernel"
// rather than as the encoding problem it is.
bool NarrowAscii(const wchar_t* text, std::string* out)
{
    out->clear();
    for (const wchar_t* p = text; *p != L'\0'; ++p) {
        if (*p < 0 || *p > 127) return false;
        out->push_back(static_cast<char>(*p));
    }
    return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options* opt)
{
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto next = [&](const wchar_t* name) -> const wchar_t* {
            if (i + 1 >= argc) {
                std::fwprintf(stderr, L"%ls requires a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == L"--list-displays") {
            opt->listDisplays = true;
        } else if (arg == L"--source") {
            const wchar_t* v = next(L"--source");
            if (!v) return false;
            opt->sourceDisplay = v;
        } else if (arg == L"--shaders") {
            const wchar_t* v = next(L"--shaders");
            if (!v) return false;
            opt->shaderDir = v;
        } else if (arg == L"--kernel") {
            const wchar_t* v = next(L"--kernel");
            if (!v) return false;
            std::string narrow;
            if (!NarrowAscii(v, &narrow)) {
                std::fwprintf(stderr, L"--kernel takes an ASCII name\n");
                return false;
            }
            try {
                opt->renderer.kernel = KernelFromName(narrow);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "%s\n", e.what());
                return false;
            }
        } else if (arg == L"--sharpness") {
            const wchar_t* v = next(L"--sharpness");
            if (!v) return false;
            opt->renderer.sharpnessStops = wcstof(v, nullptr);
        } else if (arg == L"--stretch") {
            opt->renderer.preserveAspect = false;
        } else if (arg == L"--no-cursor") {
            opt->drawCursor = false;
        } else if (arg == L"--denoise") {
            opt->renderer.denoise = true;
        } else if (arg == L"--linear") {
            opt->renderer.linearResolve = true;
        } else if (arg == L"--vsync") {
            const wchar_t* v = next(L"--vsync");
            if (!v) return false;
            opt->vsync = wcstol(v, nullptr, 10) != 0;
        } else {
            std::fwprintf(stderr, L"unknown option: %ls\n", arg.c_str());
            return false;
        }
    }
    return true;
}

int ListDisplays()
{
    std::vector<OutputInfo> outputs;
    if (FAILED(Duplicator::EnumerateOutputs(&outputs))) {
        std::fwprintf(stderr, L"no displays found\n");
        return 1;
    }
    for (const auto& o : outputs) {
        std::wprintf(L"%-16ls %5ux%-5u %ls\n", o.deviceName.c_str(),
                     o.width, o.height,
                     o.attachedToDesktop ? L"attached" : L"detached");
    }
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    Options opt;
    if (!ParseOptions(argc, argv, &opt))
        return 2;
    if (opt.listDisplays)
        return ListDisplays();

    // Per-monitor DPI awareness: without it Windows would lie about the panel's
    // pixel dimensions and the compositor would resolve to the wrong grid --
    // the one bug guaranteed to make the output look softer than native.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                        D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, device.GetAddressOf(),
                                   nullptr, context.GetAddressOf());
    if (FAILED(hr)) {
        std::fwprintf(stderr, L"D3D11CreateDevice failed: 0x%08lx\n", hr);
        return 1;
    }

    Duplicator duplicator;
    hr = duplicator.Initialize(device.Get(), opt.sourceDisplay);
    if (FAILED(hr)) {
        std::fwprintf(stderr,
                      L"could not duplicate the source display: 0x%08lx\n"
                      L"is the Visual4kDisplay driver installed and enabled?\n",
                      hr);
        return 1;
    }

    std::wprintf(L"source: %ls (%ux%u)\n", duplicator.DeviceName().c_str(),
                 duplicator.Width(), duplicator.Height());

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"Visual4kHostWindow";
    RegisterClassExW(&wc);

    const int panelW = GetSystemMetrics(SM_CXSCREEN);
    const int panelH = GetSystemMetrics(SM_CYSCREEN);

    g_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP,
                               wc.lpszClassName, L"Visual-4k",
                               WS_POPUP | WS_VISIBLE, 0, 0, panelW, panelH,
                               nullptr, nullptr, wc.hInstance, nullptr);
    if (g_window == nullptr) {
        std::fwprintf(stderr, L"CreateWindowEx failed\n");
        return 1;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    device.As(&dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDevice->GetAdapter(adapter.GetAddressOf());
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));

    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.Width = static_cast<UINT>(panelW);
    scd.Height = static_cast<UINT>(panelH);
    // R8G8B8A8 rather than the more natural BGRA: typed UAV stores to BGRA are
    // optional in D3D11 and missing on some drivers, and the final RCAS pass
    // writes through a UAV.
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_UNORDERED_ACCESS;
    scd.BufferCount = 2;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain;
    hr = factory->CreateSwapChainForHwnd(device.Get(), g_window, &scd, nullptr,
                                         nullptr, swapChain.GetAddressOf());
    if (FAILED(hr)) {
        std::fwprintf(stderr, L"CreateSwapChainForHwnd failed: 0x%08lx\n", hr);
        return 1;
    }

    // Alt+Enter would hand the panel to an exclusive-fullscreen swapchain and
    // tear the desktop we are compositing out from under DWM.
    factory->MakeWindowAssociation(g_window, DXGI_MWA_NO_ALT_ENTER);

    // Esc only works while this window has focus, and it loses focus the
    // moment you click into the virtual desktop it is displaying. Without a
    // global hotkey the only way out is Task Manager.
    if (!RegisterHotKey(g_window, kQuitHotkeyId,
                        MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12)) {
        std::fwprintf(stderr,
                      L"warning: could not register Ctrl+Alt+F12; another "
                      L"program owns it. Quit with Esc while focused, or from "
                      L"Task Manager.\n");
    }

    Renderer renderer;
    renderer.SetSettings(opt.renderer);
    hr = renderer.Initialize(device.Get(), context.Get(), opt.shaderDir);
    if (FAILED(hr)) {
        std::fwprintf(stderr, L"renderer init failed: 0x%08lx "
                              L"(shader directory: %ls)\n",
                      hr, opt.shaderDir.c_str());
        return 1;
    }

    std::wprintf(L"panel : %dx%d, kernel %hs, sharpness %.2f stops, cursor %ls\n",
                 panelW, panelH, KernelName(opt.renderer.kernel),
                 opt.renderer.sharpnessStops,
                 opt.drawCursor ? L"on" : L"off");
    std::wprintf(L"aspect: %ls\n",
                 opt.renderer.preserveAspect ? L"preserved (letterboxed if needed)"
                                             : L"stretched to fill");
    std::wprintf(L"quit  : Ctrl+Alt+F12 (or Esc while focused)\n");

    ComPtr<ID3D11Texture2D> backBuffer;
    swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));

    MSG msg = {};
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        ID3D11Texture2D* frame = nullptr;
        DXGI_OUTDUPL_FRAME_INFO info = {};
        hr = duplicator.AcquireFrame(16, &frame, &info);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // Nothing on the virtual desktop changed. The last presented frame
            // is still correct, so there is nothing to redraw.
            continue;
        }
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            // A mode change or a fullscreen app took the duplication away.
            // Rebuilding it is expected, not an error.
            duplicator.ReleaseFrame();
            if (FAILED(duplicator.Initialize(device.Get(), opt.sourceDisplay))) {
                Sleep(200);
            }
            continue;
        }
        if (FAILED(hr)) {
            std::fwprintf(stderr, L"AcquireFrame failed: 0x%08lx\n", hr);
            break;
        }

        hr = renderer.Render(frame, backBuffer.Get());

        // Desktop Duplication does not draw the pointer into the frame, so a
        // desktop without this call has no cursor at all.
        if (SUCCEEDED(hr) && opt.drawCursor) {
            const auto& pointer = duplicator.Pointer();
            if (pointer.visible && !pointer.shape.Empty()) {
                hr = renderer.DrawCursor(backBuffer.Get(), pointer.shape,
                                         pointer.shapeGeneration,
                                         pointer.x, pointer.y);
            }
        }

        duplicator.ReleaseFrame();

        if (FAILED(hr)) {
            std::fwprintf(stderr, L"Render failed: 0x%08lx\n", hr);
            break;
        }

        swapChain->Present(opt.vsync ? 1 : 0, 0);
    }

    UnregisterHotKey(g_window, kQuitHotkeyId);
    return 0;
}
