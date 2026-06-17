#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include "imgui.h"

namespace fs = std::filesystem;

// ── System icon cache ───────────────────────────────────────────────
// Extracts real Windows file-type icons (HICON → DX11 texture) and
// caches them. Extension icons stay forever; per-file icons are cleared
// on directory navigation to prevent unbounded growth.

struct D3DIcon
{
    ID3D11Texture2D*          texture = nullptr;
    ID3D11ShaderResourceView* srv     = nullptr;
};

class IconCache
{
public:
    IconCache();
    ~IconCache();

    void SetDevice(ID3D11Device* device) { m_device = device; }

    // Get an ImTextureID for a file.
    // For .exe / .lnk files: uses the real file path, caches per-path.
    // For everything else: looks up by extension.
    ImTextureID GetFileIcon(const fs::path& path);

    // Standard folder icon (cached permanently).
    ImTextureID GetFolderIcon();

    // Generic document fallback (cached permanently).
    ImTextureID GetDefaultIcon();

    // Drop all per-file cached icons.  Extension + folder + default
    // caches survive.  Call when navigating directories.
    void ClearPerFileIcons();

    static constexpr int IconSize = 16;

private:
    // Create a D3D icon, return its index in m_icons.
    size_t       CreateIcon(HICON hIcon, const std::string& tag = "");
    void         ReleaseIcon(size_t idx);

    ImTextureID  LoadCachedIcon(const std::string& ext);
    ImTextureID  LoadPerFileIcon(const fs::path& path);
    void         ReleaseAll();

    ID3D11Device* m_device = nullptr;

    // Permanent: extension → icon-index
    std::unordered_map<std::string, size_t> m_extCache;
    // Per-file: full path → icon-index (cleared on navigation)
    std::unordered_map<std::string, size_t> m_pathCache;
    // Special keys: "##folder##", "##default##" → icon-index
    std::unordered_map<std::string, size_t> m_specialCache;

    // All D3D icon resources (indexed by cache entries)
    std::vector<D3DIcon> m_icons;
};
