#pragma once
#include <Windows.h>
#include <d3d11.h>
#include "FileBrowser.h"
#include "FileOpsPanel.h"
#include "imgui.h"

struct ID3D11RenderTargetView;

class Application
{
public:
    Application(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx,
                IDXGISwapChain* swapChain, ID3D11RenderTargetView** ppRtv);
    ~Application();

    bool RenderFrame();
    void OnResize(int width, int height);
    void Init();

private:
    void SetupDockspace();
    void LoadAppIconTexture();
    void LoadAppearanceConfig();
    void SaveAppearanceConfig();
    static std::wstring GetExeDirW();

    HWND     m_hwnd;
    int      m_width  = 1280;
    int      m_height = 720;

    ID3D11Device*           m_device    = nullptr;
    ID3D11DeviceContext*    m_context   = nullptr;
    IDXGISwapChain*         m_swapChain = nullptr;
    ID3D11RenderTargetView** m_ppRtv    = nullptr;

    FileBrowser  m_fileBrowser;
    FileOpsPanel m_fileOpsPanel;

    bool m_firstFrame    = true;
    bool m_showAbout     = false;
    bool m_showSettings  = false;

    ImTextureID                 m_appIconId  = 0;
    ID3D11Texture2D*            m_appIconTex = nullptr;
    ID3D11ShaderResourceView*   m_appIconSrv = nullptr;

    ImVec4 m_clientBgColor = ImVec4(0.973f, 0.973f, 0.973f, 1);
    ImVec4 m_titleBarColor = ImVec4(0.824f, 0.851f, 0.890f, 1);
};
