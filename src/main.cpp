// FileTools v0.1 — High-performance Windows file management tool
// Win32 + DirectX 11 + Dear ImGui (docking)
#include <Windows.h>
#include <d3d11.h>
#include <tchar.h>
#include <shellapi.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "Application.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <gdiplus.h>
#include <dwmapi.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

// ── Tray icon constants ──────────────────────────────────────────
#define WM_TRAYICON  (WM_APP + 1)
#define ID_TRAY_EXIT  1001
#define ID_TRAY_SHOW  1002

// ── Globals ──────────────────────────────────────────────────────
static ID3D11Device*           g_pd3dDevice          = nullptr;
static ID3D11DeviceContext*    g_pd3dDeviceContext   = nullptr;
static IDXGISwapChain*         g_pSwapChain          = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static Application*            g_app                 = nullptr;
static NOTIFYICONDATAW         g_trayIconData        = {};
static bool                    g_trayCreated         = false;
static bool                    g_appRunning          = true;
static bool                    g_imguiReady          = false;
static bool                    g_pendingResize       = false;
static int                     g_pendingWidth        = 0;
static int                     g_pendingHeight       = 0;
static int                     g_frameCount          = 0;
static ULONG_PTR               g_gdiplusToken        = 0;
static HICON                   g_appIcon             = nullptr;

// ── Crash handler (vectored exception) ──────────────────────────
static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* pExceptionInfo)
{
    DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;
    void* addr = pExceptionInfo->ExceptionRecord->ExceptionAddress;

    spdlog::critical("!!! CRASH !!!");
    spdlog::critical("Exception code: 0x{:08X}", code);
    spdlog::critical("Exception addr: {}", addr);

    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(addr, &mbi, sizeof(mbi)))
    {
        HMODULE hMod = (HMODULE)mbi.AllocationBase;
        wchar_t modPath[MAX_PATH];
        if (GetModuleFileNameW(hMod, modPath, MAX_PATH))
        {
            char modUtf8[MAX_PATH * 4];
            WideCharToMultiByte(CP_UTF8, 0, modPath, -1, modUtf8, sizeof(modUtf8), nullptr, nullptr);
            spdlog::critical("Module: {}", modUtf8);
        }
    }

    spdlog::critical("RIP={} RSP={} RBP={}",
        (void*)pExceptionInfo->ContextRecord->Rip,
        (void*)pExceptionInfo->ContextRecord->Rsp,
        (void*)pExceptionInfo->ContextRecord->Rbp);

    spdlog::shutdown();
    return EXCEPTION_CONTINUE_SEARCH;
}

// ── Forward declarations ─────────────────────────────────────────
bool    CreateDeviceD3D(HWND hWnd);
void    CleanupDeviceD3D();
void    CreateRenderTarget();
void    CleanupRenderTarget();
bool    ResizeD3D(HWND hWnd, int width, int height);
void    CreateTrayIcon(HWND hWnd);
void    RemoveTrayIcon();
void    ShowTrayMenu(HWND hWnd);
HICON   LoadAppIcon();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ── WinMain — application entry point ────────────────────────────

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int nCmdShow)
{
    // Init spdlog — file sink to FileTools.log in exe directory
    try
    {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/FileTools.log", true);
        auto logger = std::make_shared<spdlog::logger>("filetools", fileSink);
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::debug);
        spdlog::set_default_logger(logger);
    }
    catch (...)
    {
        // If file logging fails, still try stdout
        spdlog::set_default_logger(spdlog::stderr_color_mt("filetools"));
    }

    spdlog::info("=== FileTools v0.1 starting ===");

    // Install crash handler
    AddVectoredExceptionHandler(1, CrashHandler);
    spdlog::debug("Crash handler installed");

    // Enable COM for folder picker dialogs
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    spdlog::debug("COM initialized");

    // Init GDI+ for PNG icon loading
    Gdiplus::GdiplusStartupInput gdiSI;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiSI, nullptr);
    spdlog::debug("GDI+ initialized");

    // Load custom icon — try embedded resource first (IDI_MAIN=1), then PNG file
    g_appIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    if (!g_appIcon)
    {
        spdlog::debug("Embedded icon not found, trying PNG file...");
        g_appIcon = LoadAppIcon();
    }
    spdlog::debug("App icon: {}", (void*)g_appIcon);
    if (!g_appIcon)
        g_appIcon = LoadIcon(nullptr, IDI_APPLICATION); // fallback

    // ── Register window class ──────────────────────────────
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = g_appIcon;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"FileToolsWindow";
    wc.hIconSm       = g_appIcon;
    RegisterClassExW(&wc);
    spdlog::debug("Window class registered");

    // ── Create window ──────────────────────────────────────
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int width   = 1280;
    int height  = 800;

    // Use WS_POPUP without WS_THICKFRAME — resize borders are handled
    // via WM_NCHITTEST. This avoids a sizing mismatch on first frame
    // where Windows adds thick-frame padding then NCCALCSIZE removes it.
    HWND hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"FileTools v0.1 — 文件批量管理工具",
        WS_POPUP | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX,
        (screenW - width) / 2,
        (screenH - height) / 2,
        width, height,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
    {
        spdlog::critical("CreateWindowExW failed");
        spdlog::shutdown();
        CoUninitialize();
        return 1;
    }
    spdlog::info("Window created {}x{}", width, height);

    // Enable rounded corners (Windows 11)
    int cornerPref = 3; // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/,
                          &cornerPref, sizeof(cornerPref));

    // ── Initialize DirectX 11 ──────────────────────────────
    spdlog::debug("Initializing D3D11...");
    if (!CreateDeviceD3D(hwnd))
    {
        spdlog::critical("CreateDeviceD3D failed");
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        spdlog::shutdown();
        CoUninitialize();
        return 1;
    }
    spdlog::info("D3D11 initialized OK");

    // ── Create tray icon ───────────────────────────────────
    CreateTrayIcon(hwnd);
    spdlog::debug("Tray icon created={}", g_trayCreated);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    spdlog::debug("Window shown");

    // ── Create Application ─────────────────────────────────
    spdlog::debug("Creating Application...");
    g_app = new Application(hwnd, g_pd3dDevice, g_pd3dDeviceContext,
                            g_pSwapChain, &g_mainRenderTargetView);
    spdlog::debug("Calling g_app->Init()...");
    g_app->Init();
    spdlog::info("Application initialized");
    g_imguiReady = true;

    // ── Main message loop ──────────────────────────────────
    spdlog::info("Entering main loop");
    MSG msg = {};

    while (g_appRunning)
    {
        // Process all pending Windows messages
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                spdlog::info("WM_QUIT received");
                g_appRunning = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!g_appRunning) break;

        // Process deferred resize.
        // Skip initial resize before first frame — swap chain was just created
        // at the correct auto-size and ResizeBuffers would fail with INVALID_CALL.
        if (g_pendingResize && g_frameCount > 0)
        {
            g_pendingResize = false;
            spdlog::debug("Processing resize {}x{}", g_pendingWidth, g_pendingHeight);
            ResizeD3D(hwnd, g_pendingWidth, g_pendingHeight);
        }
        else if (g_pendingResize)
        {
            g_pendingResize = false;
            spdlog::debug("Skipping initial resize {}x{} (swap chain just created)",
                          g_pendingWidth, g_pendingHeight);
        }

        // Render only when visible + RTV valid
        if (IsWindowVisible(hwnd) && g_mainRenderTargetView && g_app)
        {
            g_app->RenderFrame();
            if (++g_frameCount <= 3)
                spdlog::debug("Frame {} rendered OK", g_frameCount);
        }
        else
        {
            Sleep(16);
        }
    }

    // ── Cleanup ────────────────────────────────────────────
    spdlog::info("Cleanup — {} frames rendered", g_frameCount);
    RemoveTrayIcon();
    if (g_appIcon && g_appIcon != LoadIcon(nullptr, IDI_APPLICATION))
        DestroyIcon(g_appIcon);
    delete g_app;
    g_app = nullptr;
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    CoUninitialize();
    spdlog::info("Exit normally");
    spdlog::shutdown();

    return 0;
}

// ── DirectX 11 initialization ────────────────────────────────────

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 0;
    sd.BufferDesc.RefreshRate.Denominator = 0;
    sd.Flags                             = 0;
    sd.BufferUsage                       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                      = hWnd;
    sd.SampleDesc.Count                  = 1;
    sd.SampleDesc.Quality                = 0;
    sd.Windowed                          = TRUE;
    sd.SwapEffect                        = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        nullptr,
        &g_pd3dDeviceContext);

    if (FAILED(hr))
    {
        spdlog::error("D3D11CreateDeviceAndSwapChain FAILED hr=0x{:08X}", (uint32_t)hr);
        return false;
    }

    spdlog::debug("D3D11 device created, creating initial RTV...");
    CreateRenderTarget();
    spdlog::debug("Initial RTV: {}", (void*)g_mainRenderTargetView);
    return true;
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (FAILED(hr) || !pBackBuffer)
    {
        spdlog::warn("GetBuffer FAILED hr=0x{:08X}", (uint32_t)hr);
        g_mainRenderTargetView = nullptr;
        return;
    }
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
    spdlog::debug("  RTV created: {}", (void*)g_mainRenderTargetView);
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

void CleanupDeviceD3D()
{
    spdlog::debug("Cleaning up D3D11...");
    __try
    {
        if (g_pd3dDeviceContext)
        {
            g_pd3dDeviceContext->ClearState();
            g_pd3dDeviceContext->Flush();
        }
        CleanupRenderTarget();
        if (g_pSwapChain)       { g_pSwapChain->Release();       g_pSwapChain = nullptr; }
        if (g_pd3dDeviceContext){ g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
        if (g_pd3dDevice)       { g_pd3dDevice->Release();       g_pd3dDevice = nullptr; }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        spdlog::error("Exception during D3D cleanup (code=0x{:08X})",
                      GetExceptionCode());
    }
    spdlog::debug("D3D11 cleanup done");
}

// ── D3D11 resize ─────────────────────────────────────────────────

bool ResizeD3D(HWND hWnd, int width, int height)
{
    spdlog::debug("ResizeD3D({}, {})  frame={}", width, height, g_frameCount);

    if (!g_pd3dDevice || !g_pSwapChain) { spdlog::warn("  skip: no device"); return false; }
    if (width <= 0 || height <= 0)      { spdlog::warn("  skip: invalid size"); return false; }

    // Exact pattern from ImGui example_win32_directx11:
    // 1. Release old render target
    CleanupRenderTarget();

    // 2. Resize swap chain buffers
    HRESULT hr = g_pSwapChain->ResizeBuffers(0,
        (UINT)width, (UINT)height,
        DXGI_FORMAT_UNKNOWN, 0);
    spdlog::debug("  ResizeBuffers -> 0x{:08X} {}", (uint32_t)hr,
                  SUCCEEDED(hr) ? "OK" : "FAILED");

    // 3. Recreate render target
    CreateRenderTarget();

    // 4. Notify app — only on success
    if (SUCCEEDED(hr) && g_app)
    {
        g_app->OnResize(width, height);
        spdlog::debug("  OnResize({},{}) done", width, height);
    }
    else
    {
        spdlog::warn("  Resize FAILED — app size unchanged");
    }

    return SUCCEEDED(hr);
}

// ── Load PNG as HICON via GDI+ ──────────────────────────────────

HICON LoadAppIcon()
{
    const wchar_t* relPaths[] = {
        L"filetool.png",
        L"../res/filetool.png",
        L"../../res/filetool.png",
        L"../filetool.png",
        L"../src/filetool.png",
    };
    for (const auto* rel : relPaths) {
        Gdiplus::Bitmap bmp(rel);
        if (bmp.GetLastStatus() == Gdiplus::Ok) {
            HICON hIcon = nullptr;
            if (bmp.GetHICON(&hIcon) == Gdiplus::Ok && hIcon) {
                spdlog::info("App icon loaded OK");
                return hIcon;
            }
        }
    }
    spdlog::warn("Could not load filetool.png from relative paths");
    return nullptr;
}
// ── System tray icon ─────────────────────────────────────────────

void CreateTrayIcon(HWND hWnd)
{
    g_trayIconData.cbSize           = sizeof(NOTIFYICONDATAW);
    g_trayIconData.hWnd             = hWnd;
    g_trayIconData.uID              = 1;
    g_trayIconData.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_trayIconData.uCallbackMessage = WM_TRAYICON;
    g_trayIconData.hIcon            = g_appIcon ? g_appIcon : LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_trayIconData.szTip, L"FileTools — 文件批量管理工具");
    g_trayCreated = Shell_NotifyIconW(NIM_ADD, &g_trayIconData) != FALSE;
}

void RemoveTrayIcon()
{
    if (g_trayCreated)
    {
        Shell_NotifyIconW(NIM_DELETE, &g_trayIconData);
        g_trayCreated = false;
    }
}

void ShowTrayMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SHOW, L"显示窗口");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   pt.x, pt.y, 0, hWnd, nullptr);
    DestroyMenu(hMenu);
}

// ── Window procedure ─────────────────────────────────────────────

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_imguiReady && ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_NCCALCSIZE:
        // Remove standard title bar area (we have custom title bar)
        if (wParam == TRUE) return 0;
        break;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 800;
        mmi->ptMinTrackSize.y = 500;
        return 0;
    }

    case WM_NCHITTEST:
    {
        // Allow window resize from edges (borderless window)
        LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam);
        if (hit == HTCLIENT)
        {
            POINT pt{(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
            RECT rc; GetWindowRect(hWnd, &rc);
            int border = 6; // resize border width in pixels

            if (pt.y < rc.top + border)
            {
                if (pt.x < rc.left + border)       return HTTOPLEFT;
                if (pt.x > rc.right - border)      return HTTOPRIGHT;
                return HTTOP;
            }
            if (pt.y > rc.bottom - border)
            {
                if (pt.x < rc.left + border)       return HTBOTTOMLEFT;
                if (pt.x > rc.right - border)      return HTBOTTOMRIGHT;
                return HTBOTTOM;
            }
            if (pt.x < rc.left + border)           return HTLEFT;
            if (pt.x > rc.right - border)          return HTRIGHT;
        }
        return hit;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
        {
            g_pendingResize = true;
            g_pendingWidth  = (int)LOWORD(lParam);
            g_pendingHeight = (int)HIWORD(lParam);
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU)
            return 0;
        break;

    case WM_CLOSE:
        spdlog::debug("WM_CLOSE -> hide to tray");
        ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        spdlog::debug("WM_DESTROY -> PostQuitMessage");
        PostQuitMessage(0);
        return 0;

    case WM_DPICHANGED:
    {
        RECT* suggested = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_TRAYICON:
    {
        UINT trayMsg = (UINT)lParam;
        if (trayMsg == WM_LBUTTONDBLCLK || trayMsg == WM_LBUTTONUP)
        {
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
        }
        else if (trayMsg == WM_RBUTTONUP)
        {
            ShowTrayMenu(hWnd);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case ID_TRAY_SHOW:
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        return 0;
    }
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}
