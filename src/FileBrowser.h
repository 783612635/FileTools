#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <filesystem>
#include <vector>
#include <string>
#include <functional>
#include <cstdint>
#include <unordered_set>
#include "IconCache.h"

namespace fs = std::filesystem;

// ── A single entry shown in the file list ────────────────────────
struct FileEntry
{
    fs::path     path;
    std::string  name;         // display name (UTF-8)
    std::string  extension;    // lower-case, e.g. ".mp4"
    uintmax_t    fileSize = 0;
    uint64_t     lastWriteTime = 0; // FILETIME as uint64
    bool         isDirectory = false;
    bool         isHidden    = false;
    bool         isDrive     = false;
    bool         isSymlink   = false;
};

// ── A logical drive ──────────────────────────────────────────────
struct DriveInfo
{
    wchar_t     letter;        // e.g. L'C'
    std::string rootPath;     // "C:\\"
    std::string displayName;  // "Local Disk (C:)"
    UINT        driveType;
};

// ── Quick-access shortcut (Desktop, etc.) ───────────────────────
struct QuickAccess
{
    std::string name;
    fs::path    path;
    std::string icon;  // UTF-8 MDL2 icon
};

// ── Callback type when user requests adding files to operations ──
using AddFilesCallback = std::function<void(const std::vector<fs::path>& paths)>;

// ── FileBrowser panel ────────────────────────────────────────────
class FileBrowser
{
public:
    FileBrowser();
    ~FileBrowser() = default;

    void SetAddFilesCallback(AddFilesCallback cb) { m_addFilesCb = std::move(cb); }

    // Must call once before rendering — provides the D3D11 device for icon textures.
    void SetDevice(ID3D11Device* device) { m_iconCache.SetDevice(device); }

    // Hidden files preference
    bool IsShowHidden() const { return m_showHidden; }
    void SetShowHidden(bool show);

    // Load/save user preferences (INI format)
    void LoadConfig();
    void SaveConfig();

    // Render the file browser panel. Returns true if panel is open.
    bool Render();

private:
    // ── Drives / Quick Access ──────────────────────────────────
    void RefreshDrives();
    void InitQuickAccess();
    void RenderDriveBar();

    // ── Directory listing ────────────────────────────────────
    void NavigateTo(const fs::path& path);
    void GoBack();
    void GoForward();
    void GoUp();
    void RefreshCurrentDir();
    void RenderPathBar();
    void RenderFileTable();
    void RenderContextMenu();

    // ── Selection helpers ────────────────────────────────────
    bool IsPathSelected(const fs::path& p) const;
    void SelectSingle(const fs::path& p);
    void ToggleSelect(const fs::path& p);
    void SelectRange(int fromIdx, int toIdx, const std::vector<const FileEntry*>& visible);
    void ClearSelection();

    // ── Helpers ──────────────────────────────────────────────
    static std::string FormatSize(uintmax_t bytes);
    static std::string FormatTime(uint64_t filetime);
    std::string        PathToUtf8(const fs::path& p) const;
    static bool        HasChildren(const fs::path& dir);

    // ── State ────────────────────────────────────────────────
    std::vector<DriveInfo>   m_drives;
    std::vector<QuickAccess> m_quickAccess;
    fs::path                 m_currentPath;
    std::vector<FileEntry>   m_entries;
    std::vector<fs::path>    m_historyBack;
    std::vector<fs::path>    m_historyForward;
    std::vector<fs::path>    m_selectedPaths;   // currently multi-selected files
    fs::path                 m_selectedPath;     // last single right-click selected path
    int                      m_anchorIndex = -1;// index in *visible* list for Shift+Click range
    bool                     m_showHidden  = false;

    AddFilesCallback         m_addFilesCb;
    IconCache                m_iconCache;

    // ── Animation ────────────────────────────────────────────
    float m_animProgress = 0.0f;
};
