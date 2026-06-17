#include "IconCache.h"
#include <shlobj.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>
#include <vector>
#include <cstring>
#include <cstdint>

// ── Sentinel colour used as background when rendering icons ──────
static constexpr uint8_t SENTINEL_R = 255;
static constexpr uint8_t SENTINEL_G = 0;
static constexpr uint8_t SENTINEL_B = 255;
static constexpr int    SENTINEL_THRESHOLD = 40;

static bool isSentinel(uint8_t r, uint8_t g, uint8_t b)
{
    int dr = (int)r - SENTINEL_R;
    int dg = (int)g - SENTINEL_G;
    int db = (int)b - SENTINEL_B;
    return (dr*dr + dg*dg + db*db) < (SENTINEL_THRESHOLD * SENTINEL_THRESHOLD);
}

// ── Constructor / Destructor ──────────────────────────────────────

IconCache::IconCache()  { spdlog::debug("IconCache created"); }
IconCache::~IconCache() { spdlog::debug("IconCache destroyed"); ReleaseAll(); }

void IconCache::ReleaseAll()
{
    for (auto& icon : m_icons)
    {
        if (icon.srv)     icon.srv->Release();
        if (icon.texture) icon.texture->Release();
    }
    m_icons.clear();
    m_extCache.clear();
    m_pathCache.clear();
    m_specialCache.clear();
}

// ── Public API ──────────────────────────────────────────────────

ImTextureID IconCache::GetFileIcon(const fs::path& path)
{
    if (!m_device) return 0;

    std::string ext;
    if (path.has_extension())
    {
        ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    }

    if (ext.empty())
        return GetDefaultIcon();

    // Per-file icons (.exe, .lnk etc.) — cache by full path
    if (ext == ".exe" || ext == ".dll" || ext == ".lnk" || ext == ".ico" || ext == ".scr")
    {
        std::string key = path.string();
        auto it = m_pathCache.find(key);
        if (it != m_pathCache.end())
            return reinterpret_cast<ImTextureID>(m_icons[it->second].srv);
        return LoadPerFileIcon(path);
    }

    // Extension-based cache
    auto it = m_extCache.find(ext);
    if (it != m_extCache.end())
        return reinterpret_cast<ImTextureID>(m_icons[it->second].srv);

    return LoadCachedIcon(ext);
}

ImTextureID IconCache::GetFolderIcon()
{
    if (!m_device) return 0;

    auto it = m_specialCache.find("##folder##");
    if (it != m_specialCache.end())
        return reinterpret_cast<ImTextureID>(m_icons[it->second].srv);

    SHFILEINFOW shfi{};
    if (SHGetFileInfoW(L"dummy", FILE_ATTRIBUTE_DIRECTORY, &shfi, sizeof(shfi),
        SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
    {
        size_t idx = CreateIcon(shfi.hIcon, "folder");
        DestroyIcon(shfi.hIcon);
        if (idx != (size_t)-1)
        {
            m_specialCache["##folder##"] = idx;
            return reinterpret_cast<ImTextureID>(m_icons[idx].srv);
        }
    }
    return GetDefaultIcon();
}

ImTextureID IconCache::GetDefaultIcon()
{
    if (!m_device) return 0;

    auto it = m_specialCache.find("##default##");
    if (it != m_specialCache.end())
        return reinterpret_cast<ImTextureID>(m_icons[it->second].srv);

    SHFILEINFOW shfi{};
    if (SHGetFileInfoW(L"dummy.txt", FILE_ATTRIBUTE_NORMAL, &shfi, sizeof(shfi),
        SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
    {
        size_t idx = CreateIcon(shfi.hIcon, "default");
        DestroyIcon(shfi.hIcon);
        if (idx != (size_t)-1)
        {
            m_specialCache["##default##"] = idx;
            return reinterpret_cast<ImTextureID>(m_icons[idx].srv);
        }
    }
    return 0;
}

void IconCache::ClearPerFileIcons()
{
    if (m_pathCache.empty()) return;

    spdlog::debug("IconCache: clearing {} per-file icons", m_pathCache.size());

    // Collect indices to release
    std::vector<size_t> toRelease;
    for (auto& kv : m_pathCache)
        toRelease.push_back(kv.second);

    m_pathCache.clear();

    // Release D3D objects for per-file icons
    // (we leave holes in m_icons — that's fine, entries are small)
    for (size_t idx : toRelease)
        ReleaseIcon(idx);

    spdlog::debug("IconCache: released {} D3D icon objects", toRelease.size());
}

// ── CreateIcon: HICON → D3D texture + SRV ──────────────────────

size_t IconCache::CreateIcon(HICON hIcon, const std::string& tag)
{
    if (!m_device || !hIcon) return (size_t)-1;

    int sz = IconSize;

    // ── Get icon info for diagnostics ────────────────────────
    ICONINFO ii{};
    if (!GetIconInfo(hIcon, &ii)) return (size_t)-1;

    BITMAP bmColor{};
    if (ii.hbmColor) GetObject(ii.hbmColor, sizeof(bmColor), &bmColor);
    BITMAP bmMask{};
    if (ii.hbmMask)  GetObject(ii.hbmMask, sizeof(bmMask), &bmMask);

    // ── Render icon to 32-bit DIB ────────────────────────────
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);

    BITMAPINFOHEADER bih{};
    bih.biSize        = sizeof(bih);
    bih.biWidth       = sz;
    bih.biHeight      = -sz;  // top-down
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;

    uint8_t* dibBits = nullptr;
    HBITMAP hDib = CreateDIBSection(hdcMem, (BITMAPINFO*)&bih, DIB_RGB_COLORS,
                                    (void**)&dibBits, nullptr, 0);
    if (!hDib || !dibBits)
    {
        DeleteDC(hdcMem); ReleaseDC(nullptr, hdcScreen);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
        if (ii.hbmMask)  DeleteObject(ii.hbmMask);
        return (size_t)-1;
    }

    HBITMAP oldBmp = (HBITMAP)SelectObject(hdcMem, hDib);

    // Fill with sentinel magenta
    for (int i = 0; i < sz * sz; ++i)
    {
        dibBits[i * 4 + 0] = SENTINEL_B;
        dibBits[i * 4 + 1] = SENTINEL_G;
        dibBits[i * 4 + 2] = SENTINEL_R;
        dibBits[i * 4 + 3] = 0;
    }

    DrawIconEx(hdcMem, 0, 0, hIcon, sz, sz, 0, nullptr, DI_NORMAL);
    GdiFlush();

    // ── Build RGBA pixels ────────────────────────────────────
    std::vector<uint8_t> pixels(sz * sz * 4, 0);
    for (int i = 0; i < sz * sz; ++i)
    {
        uint8_t b = dibBits[i * 4 + 0];
        uint8_t g = dibBits[i * 4 + 1];
        uint8_t r = dibBits[i * 4 + 2];

        if (!isSentinel(r, g, b))
        {
            uint8_t a = 255;
            pixels[i * 4 + 0] = (uint8_t)((r * a) / 255);
            pixels[i * 4 + 1] = (uint8_t)((g * a) / 255);
            pixels[i * 4 + 2] = (uint8_t)((b * a) / 255);
            pixels[i * 4 + 3] = a;
        }
    }

    // ── GDI cleanup ──────────────────────────────────────────
    SelectObject(hdcMem, oldBmp);
    DeleteObject(hDib);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask)  DeleteObject(ii.hbmMask);

    // ── Create D3D texture ───────────────────────────────────
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = sz;
    desc.Height           = sz;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subData{};
    subData.pSysMem     = pixels.data();
    subData.SysMemPitch = sz * 4;

    D3DIcon icon{};
    if (FAILED(m_device->CreateTexture2D(&desc, &subData, &icon.texture)) || !icon.texture)
        return (size_t)-1;

    if (FAILED(m_device->CreateShaderResourceView(icon.texture, nullptr, &icon.srv)) || !icon.srv)
    {
        icon.texture->Release();
        return (size_t)-1;
    }

    m_icons.push_back(icon);
    spdlog::debug("IconCache: created icon [{}] idx={}", tag, m_icons.size() - 1);
    return m_icons.size() - 1;
}

void IconCache::ReleaseIcon(size_t idx)
{
    if (idx >= m_icons.size()) return;
    auto& icon = m_icons[idx];
    if (icon.srv)     { icon.srv->Release();     icon.srv = nullptr; }
    if (icon.texture) { icon.texture->Release(); icon.texture = nullptr; }
}

// ── LoadCachedIcon (extension-based) ────────────────────────────

ImTextureID IconCache::LoadCachedIcon(const std::string& ext)
{
    std::wstring dummy = L"dummy" + std::wstring(ext.begin(), ext.end());

    SHFILEINFOW shfi{};
    if (!SHGetFileInfoW(dummy.c_str(), FILE_ATTRIBUTE_NORMAL, &shfi, sizeof(shfi),
        SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
        return GetDefaultIcon();

    if (!shfi.hIcon) return GetDefaultIcon();

    size_t idx = CreateIcon(shfi.hIcon, ext);
    DestroyIcon(shfi.hIcon);

    if (idx == (size_t)-1) return GetDefaultIcon();

    m_extCache[ext] = idx;
    return reinterpret_cast<ImTextureID>(m_icons[idx].srv);
}

// ── LoadPerFileIcon (.exe / .lnk with real icons) ───────────────

ImTextureID IconCache::LoadPerFileIcon(const fs::path& path)
{
    std::wstring wpath = path.wstring();

    SHFILEINFOW shfi{};
    if (!SHGetFileInfoW(wpath.c_str(), 0, &shfi, sizeof(shfi),
                        SHGFI_ICON | SHGFI_SMALLICON))
    {
        // Fallback to extension icon
        std::string ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        return LoadCachedIcon(ext);
    }

    if (!shfi.hIcon)
    {
        std::string ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        return LoadCachedIcon(ext);
    }

    size_t idx = CreateIcon(shfi.hIcon, path.filename().string());
    DestroyIcon(shfi.hIcon);

    if (idx == (size_t)-1)
    {
        std::string ext = path.extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        return LoadCachedIcon(ext);
    }

    // Cache by full path — WILL be freed on directory navigation
    m_pathCache[path.string()] = idx;
    return reinterpret_cast<ImTextureID>(m_icons[idx].srv);
}
