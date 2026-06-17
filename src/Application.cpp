#include "Application.h"
#include "Theme.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <spdlog/spdlog.h>
#include <gdiplus.h>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ── Helpers ──────────────────────────────────────────────────────

static std::string WstrToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWstr(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), ws.data(), len);
    return ws;
}

std::wstring Application::GetExeDirW()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    return p.substr(0, p.find_last_of(L"\\/") + 1);
}

// ── Constructor / Destructor ──────────────────────────────────────

Application::Application(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx,
                         IDXGISwapChain* swapChain, ID3D11RenderTargetView** ppRtv)
    : m_hwnd(hwnd), m_device(device), m_context(ctx), m_swapChain(swapChain), m_ppRtv(ppRtv) {}

Application::~Application()
{
    m_fileBrowser.SaveConfig();
    SaveAppearanceConfig();
    if (m_appIconSrv) m_appIconSrv->Release();
    if (m_appIconTex) m_appIconTex->Release();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

// ── Init ──────────────────────────────────────────────────────────

void Application::Init()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = false;
    io.IniFilename = nullptr;

    float fontSize = 16.0f;

    {
        ImFontConfig cfg;
        cfg.OversampleH = 2; cfg.OversampleV = 2;
        static const ImWchar ranges[] = {
            0x0020,0x00FF,0x2000,0x206F,0x3000,0x30FF,
            0x31F0,0x31FF,0xFF00,0xFFEF,0x4E00,0x9FFF,0x3400,0x4DBF,0,
        };
        ImFont* f = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", fontSize, &cfg, ranges);
        cfg.MergeMode = true;
        f = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", fontSize, &cfg, io.Fonts->GetGlyphRangesDefault());
    }
    {
        ImFontConfig cfg; cfg.MergeMode = true; cfg.OversampleH = 1; cfg.OversampleV = 1;
        static const ImWchar mdl2Ranges[] = { 0xE000, 0xF8FF, 0 };
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segmdl2.ttf", fontSize, &cfg, mdl2Ranges);
    }
    io.Fonts->Build();

    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_device, m_context);
    Theme::ApplyTheme();

    m_fileBrowser.SetDevice(m_device);
    m_fileBrowser.SetAddFilesCallback([this](const std::vector<fs::path>& paths) {
        m_fileOpsPanel.AddFiles(paths);
    });

    LoadAppIconTexture();
    LoadAppearanceConfig();
    Theme::UpdateFromClientBg(m_clientBgColor);
}

// ── Resize ────────────────────────────────────────────────────────

void Application::OnResize(int width, int height)
{
    m_width  = width;
    m_height = height;
}

// ── Appearance config (INI) ───────────────────────────────────────

void Application::LoadAppearanceConfig()
{
    std::wstring iniPath = GetExeDirW() + L"FileTools.ini";
    std::ifstream f(iniPath);
    if (!f.is_open()) return;

    std::string line, section;
    while (std::getline(f, line))
    {
        size_t s = line.find_first_not_of(" \t\r");
        if (s == std::string::npos) continue;
        size_t e = line.find_last_not_of(" \t\r");
        line = line.substr(s, e - s + 1);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[' && line.back() == ']')
        {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // trim
        while (!key.empty() && (key.back()==' '||key.back()=='\t')) key.pop_back();
        while (!val.empty() && (val.front()==' '||val.front()=='\t')) val.erase(0,1);

        if (section == "Appearance")
        {
            if (key == "ClientBgColor")
            {
                float r, g, b;
                if (sscanf(val.c_str(), "%f,%f,%f", &r, &g, &b) == 3)
                    m_clientBgColor = ImVec4(r, g, b, 1);
            }
            // (BgImagePath legacy key — ignored)
        }
    }

    // Compute title bar contrast
    float lum = 0.299f*m_clientBgColor.x + 0.587f*m_clientBgColor.y + 0.114f*m_clientBgColor.z;
    m_titleBarColor = lum > 0.5f
        ? ImVec4(m_clientBgColor.x*0.82f, m_clientBgColor.y*0.82f, m_clientBgColor.z*0.82f, 1)
        : ImVec4(std::min(m_clientBgColor.x*1.18f,1.0f), std::min(m_clientBgColor.y*1.18f,1.0f),
                 std::min(m_clientBgColor.z*1.18f,1.0f), 1);
}

void Application::SaveAppearanceConfig()
{
    std::wstring iniPath = GetExeDirW() + L"FileTools.ini";
    // Read existing, replace [Appearance] section
    std::vector<std::string> lines;
    bool inAppearance = false, wroteAppearance = false;
    {
        std::ifstream f(iniPath);
        if (f.is_open())
        {
            std::string line;
            while (std::getline(f, line))
            {
                size_t s = line.find_first_not_of(" \t\r");
                std::string t = (s==std::string::npos)?"":line.substr(s);
                if (!t.empty() && t[0]=='[' && t.back()==']')
                {
                    if (inAppearance && !wroteAppearance)
                    {
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(3)
                            << m_clientBgColor.x << "," << m_clientBgColor.y << "," << m_clientBgColor.z;
                        lines.push_back("ClientBgColor=" + oss.str());
                        wroteAppearance = true;
                    }
                    inAppearance = (t.substr(1,t.size()-2) == "Appearance");
                }
                else if (inAppearance && !wroteAppearance &&
                         t.find("ClientBgColor=")==0)
                {
                    continue; // skip, will be replaced
                }
                lines.push_back(line);
            }
        }
    }
    if (!wroteAppearance)
    {
        if (!lines.empty() && !lines.back().empty()) lines.push_back("");
        lines.push_back("[Appearance]");
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3)
            << m_clientBgColor.x << "," << m_clientBgColor.y << "," << m_clientBgColor.z;
        lines.push_back("ClientBgColor=" + oss.str());
    }

    std::ofstream f(iniPath);
    if (!f.is_open()) return;
    for (size_t i = 0; i < lines.size(); ++i)
        f << lines[i] << "\n";
}

// ── App icon ──────────────────────────────────────────────────────

void Application::LoadAppIconTexture()
{
    std::wstring iconPath = GetExeDirW() + L"filetool.png";
    if (GetFileAttributesW(iconPath.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    Gdiplus::Bitmap bmp(iconPath.c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) return;

    HICON hIcon = nullptr;
    if (bmp.GetHICON(&hIcon) != Gdiplus::Ok || !hIcon) return;

    int iconSz = 24;
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(bih); bih.biWidth = iconSz; bih.biHeight = -iconSz;
    bih.biPlanes = 1; bih.biBitCount = 32; bih.biCompression = BI_RGB;

    uint8_t* dibBits = nullptr;
    HBITMAP hDib = CreateDIBSection(hdcMem, (BITMAPINFO*)&bih, DIB_RGB_COLORS, (void**)&dibBits, nullptr, 0);
    if (!hDib) { DestroyIcon(hIcon); DeleteDC(hdcMem); ReleaseDC(nullptr, hdcScreen); return; }

    HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, hDib);
    for (int i = 0; i < iconSz*iconSz; ++i) {
        dibBits[i*4+0]=255; dibBits[i*4+1]=0; dibBits[i*4+2]=255; dibBits[i*4+3]=0;
    }
    DrawIconEx(hdcMem, 0, 0, hIcon, iconSz, iconSz, 0, nullptr, DI_NORMAL);
    GdiFlush();

    std::vector<uint8_t> pixels(iconSz*iconSz*4);
    for (int i = 0; i < iconSz*iconSz; ++i) {
        if (dibBits[i*4+0] < 200 || dibBits[i*4+1] > 50 || dibBits[i*4+2] < 200) {
            pixels[i*4+0]=dibBits[i*4+2]; pixels[i*4+1]=dibBits[i*4+1];
            pixels[i*4+2]=dibBits[i*4+0]; pixels[i*4+3]=255;
        }
    }
    SelectObject(hdcMem, oldBmp); DeleteObject(hDib); DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen); DestroyIcon(hIcon);

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width=iconSz; desc.Height=iconSz; desc.MipLevels=1; desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub{}; sub.pSysMem=pixels.data(); sub.SysMemPitch=iconSz*4;
    if (SUCCEEDED(m_device->CreateTexture2D(&desc, &sub, &m_appIconTex)) && m_appIconTex)
    {
        m_device->CreateShaderResourceView(m_appIconTex, nullptr, &m_appIconSrv);
        if (m_appIconSrv) m_appIconId = reinterpret_cast<ImTextureID>(m_appIconSrv);
    }
    spdlog::info("App icon: ✓ created");
}

void Application::SetupDockspace()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float titleH = Theme::TitleBarHeight;
    float btnW = 46;

    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags wflags =
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m_clientBgColor);

    ImGui::Begin("MainWindow", nullptr, wflags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    float winW = ImGui::GetWindowWidth();

    // ── Title bar ──────────────────────────────────────────
    {
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(vp->Pos.x, vp->Pos.y),
            ImVec2(vp->Pos.x + winW, vp->Pos.y + titleH),
            ImColor(m_titleBarColor));

        // Determine text color based on title bar luminance
        float tlum = 0.299f*m_titleBarColor.x + 0.587f*m_titleBarColor.y + 0.114f*m_titleBarColor.z;
        ImVec4 titleTextCol = tlum > 0.5f ? ImVec4(0.1f,0.1f,0.1f,1) : ImVec4(0.95f,0.95f,0.95f,1);
        ImU32 btnHoverCol = tlum > 0.5f ? IM_COL32(180,180,180,255) : IM_COL32(80,80,80,255);

        ImGui::BeginChild("##TitleChild", ImVec2(winW, titleH), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);

        // System buttons
        float bx = winW - btnW;
        // Close
        {
            ImRect bb(ImVec2(bx,0), ImVec2(bx+btnW,titleH));
            bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
            ImGui::GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max,
                hovered ? IM_COL32(232,17,35,255) : IM_COL32(0,0,0,0));
            float cx = bx+btnW/2, cy = titleH/2, s = 5;
            ImU32 xcol = hovered ? IM_COL32(255,255,255,255) : ImColor(titleTextCol);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(cx-s,cy-s), ImVec2(cx+s,cy+s), xcol, 1.5f);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(cx+s,cy-s), ImVec2(cx-s,cy+s), xcol, 1.5f);
            ImGui::SetCursorPos(ImVec2(bx, 0));
            if (ImGui::InvisibleButton("##Close", ImVec2(btnW, titleH)))
                ::ShowWindow(m_hwnd, SW_HIDE);
        }
        bx -= btnW;
        // Maximize
        {
            ImRect bb(ImVec2(bx,0), ImVec2(bx+btnW,titleH));
            bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
            ImGui::GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max,
                hovered ? ImColor(btnHoverCol) : IM_COL32(0,0,0,0));
            ImU32 col = ImColor(titleTextCol);
            float cx = bx+btnW/2, cy = titleH/2, s = 6;
            ImGui::GetWindowDrawList()->AddRect(ImVec2(cx-s,cy-s), ImVec2(cx+s,cy+s), col, 0, 0, 1.2f);
            ImGui::SetCursorPos(ImVec2(bx, 0));
            if (ImGui::InvisibleButton("##Max", ImVec2(btnW, titleH)))
            {
                WINDOWPLACEMENT wp{sizeof(wp)}; GetWindowPlacement(m_hwnd, &wp);
                ShowWindow(m_hwnd, (wp.showCmd==SW_MAXIMIZE)?SW_RESTORE:SW_MAXIMIZE);
            }
        }
        bx -= btnW;
        // Minimize
        {
            ImRect bb(ImVec2(bx,0), ImVec2(bx+btnW,titleH));
            bool hovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
            ImGui::GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max,
                hovered ? ImColor(btnHoverCol) : IM_COL32(0,0,0,0));
            ImU32 col = ImColor(titleTextCol);
            float cx = bx+btnW/2, cy = titleH/2;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(cx-6,cy), ImVec2(cx+6,cy), col, 1.2f);
            ImGui::SetCursorPos(ImVec2(bx, 0));
            if (ImGui::InvisibleButton("##Min", ImVec2(btnW, titleH)))
                ShowWindow(m_hwnd, SW_MINIMIZE);
        }

        // Left side: icon + title + buttons
        float textH = ImGui::GetTextLineHeight();
        float btnH  = textH + 6.0f;
        float textY = (titleH - textH)*0.5f;
        float btnY  = (titleH - btnH)*0.5f;
        float padY  = (btnH - textH)*0.5f;
        float iconSz = 20.0f;
        float iconY = (titleH - iconSz)*0.5f;

        float curX = 8.0f;
        if (m_appIconId)
        {
            ImGui::SetCursorPos(ImVec2(curX, iconY));
            ImGui::Image(m_appIconId, ImVec2(iconSz, iconSz));
            curX += iconSz + 6.0f;
        }
        ImGui::SetCursorPos(ImVec2(curX, textY));
        ImGui::TextColored(titleTextCol, "FileTools v0.1");
        curX += ImGui::CalcTextSize("FileTools v0.1").x + 16.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, padY));
        ImGui::PushStyleColor(ImGuiCol_Text, titleTextCol);

        ImGui::SetCursorPos(ImVec2(curX, btnY));
        if (ImGui::Button("关于", ImVec2(0, btnH))) m_showAbout = true;
        curX += ImGui::GetItemRectSize().x + 6.0f;

        ImGui::SetCursorPos(ImVec2(curX, btnY));
        if (ImGui::Button("设置", ImVec2(0, btnH))) m_showSettings = true;
        curX += ImGui::GetItemRectSize().x + 6.0f;

        ImGui::SetCursorPos(ImVec2(curX, btnY));
        if (ImGui::Button("重置布局", ImVec2(0, btnH))) m_firstFrame = true;

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        float dragX = curX + ImGui::GetItemRectSize().x + 12.0f;
        float dragW = bx - dragX;
        if (dragW > 20)
        {
            ImGui::SetCursorPos(ImVec2(dragX, 0));
            ImGui::InvisibleButton("##DragTitle", ImVec2(dragW, titleH));
            static bool dragging = false; static POINT dragOff{};
            if (ImGui::IsItemActivated())
            {
                dragging = true; POINT cp; GetCursorPos(&cp);
                RECT rc; GetWindowRect(m_hwnd, &rc);
                dragOff.x = cp.x - rc.left; dragOff.y = cp.y - rc.top;
            }
            if (dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                POINT cp; GetCursorPos(&cp);
                SetWindowPos(m_hwnd, nullptr, cp.x-dragOff.x, cp.y-dragOff.y, 0,0, SWP_NOSIZE|SWP_NOZORDER);
            }
            if (ImGui::IsItemDeactivated()) dragging = false;
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                WINDOWPLACEMENT wp{sizeof(wp)}; GetWindowPlacement(m_hwnd, &wp);
                ShowWindow(m_hwnd, (wp.showCmd==SW_MAXIMIZE)?SW_RESTORE:SW_MAXIMIZE);
            }
        }
        ImGui::EndChild();
    }

    // DockSpace
    float topOffset = ImGui::GetCursorPosY();
    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0, vp->Size.y - topOffset), ImGuiDockNodeFlags_PassthruCentralNode);

    if (m_firstFrame)
    {
        m_firstFrame = false;
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImVec2(vp->Size.x, vp->Size.y - topOffset));
        ImGuiID dockLeft, dockRight;
        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.28f, &dockLeft, &dockRight);
        ImGui::DockBuilderDockWindow("文件操作列表", dockLeft);
        ImGui::DockBuilderDockWindow("文件浏览器",   dockRight);
        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::End();
}

// ── RenderFrame ───────────────────────────────────────────────────

bool Application::RenderFrame()
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Draw bg image on dock node background (behind docked windows only)
    {
        ImGuiID dockId = ImGui::GetID("MainDockSpace");
        {
        }
    }

    SetupDockspace();

    // About popup
    if (m_showAbout) { ImGui::OpenPopup("关于 FileTools"); m_showAbout = false; }
    if (ImGui::BeginPopupModal("关于 FileTools", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("FileTools v0.1");
        ImGui::Spacing();
        ImGui::Text("高性能 Windows 文件批量管理工具");
        ImGui::Separator(); ImGui::Spacing();
        ImGui::BulletText("浏览所有磁盘和文件");
        ImGui::BulletText("批量修改文件后缀名 / 删字符 / 移动");
        ImGui::BulletText("7z 压缩 / 解压（zip 自动降级）");
        ImGui::Spacing();
        ImGui::TextColored(Theme::TextDim, "Built with Dear ImGui + DirectX 11");
        ImGui::Spacing();
        if (ImGui::Button("确定", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Settings popup
    if (m_showSettings) { ImGui::OpenPopup("设置"); m_showSettings = false; }
    if (ImGui::BeginPopupModal("设置", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("偏好设置");
        ImGui::Separator();
        ImGui::Spacing();

        // ── Show hidden files ──────────────────────────────
        bool showHidden = m_fileBrowser.IsShowHidden();
        if (ImGui::Checkbox("默认显示隐藏文件和文件夹", &showHidden))
            m_fileBrowser.SetShowHidden(showHidden);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("外观设置");
        ImGui::Spacing();

        // ── Client area background color ────────────────────
        float col[3] = {m_clientBgColor.x, m_clientBgColor.y, m_clientBgColor.z};
        if (ImGui::ColorEdit3("客户区背景色", col, ImGuiColorEditFlags_NoInputs))
        {
            m_clientBgColor = ImVec4(col[0], col[1], col[2], 1);
            float lum = 0.299f*col[0] + 0.587f*col[1] + 0.114f*col[2];
            m_titleBarColor = lum > 0.5f
                ? ImVec4(col[0]*0.82f, col[1]*0.82f, col[2]*0.82f, 1)
                : ImVec4(std::min(col[0]*1.18f,1.0f), std::min(col[1]*1.18f,1.0f),
                         std::min(col[2]*1.18f,1.0f), 1);
            // Update all derived theme colours (file browser, ops panel, etc.)
            Theme::UpdateFromClientBg(m_clientBgColor);
            SaveAppearanceConfig();
        }
        ImGui::SetItemTooltip("标题栏颜色会根据此颜色自动调整为对比色");

        if (ImGui::Button("恢复默认背景色"))
        {
            m_clientBgColor = ImVec4(0.973f, 0.973f, 0.973f, 1);
            m_titleBarColor = ImVec4(0.824f, 0.851f, 0.890f, 1);
            Theme::UpdateFromClientBg(m_clientBgColor);
            SaveAppearanceConfig();
        }

        ImGui::Spacing();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(Theme::TextDim, "7za.exe 需要手动下载放置到程序目录:");
        ImGui::TextColored(Theme::TextDim, "https://www.7-zip.org/download.html");
        ImGui::TextColored(Theme::TextDim, "选 \"7-Zip Extra: standalone console version\"");

        ImGui::Spacing();
        if (ImGui::Button("关闭", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    m_fileOpsPanel.Render();
    m_fileBrowser.Render();
    ImGui::Render();

    ID3D11RenderTargetView* rtv = *m_ppRtv;
    m_context->OMSetRenderTargets(1, &rtv, nullptr);
    float clear[4] = {m_clientBgColor.x, m_clientBgColor.y, m_clientBgColor.z, 1.0f};
    m_context->ClearRenderTargetView(rtv, clear);

    // Draw bg image BEFORE ImGui (game-engine style)

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swapChain->Present(1, 0);
    return true;
}
