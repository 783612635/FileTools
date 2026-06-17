#include "FileBrowser.h"
#include "Theme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstring>
#include <cwctype>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>

// ── Windows helpers ──────────────────────────────────────────────

static std::string WstrToUtf8(std::wstring_view ws)
{
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWstr(std::string_view s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (len <= 0) return {};
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), ws.data(), len);
    return ws;
}

static uint64_t FileTimeToUint64(const FILETIME& ft)
{
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

static bool GetFileAttributesExWide(const fs::path& p, DWORD& attrs, FILETIME& ftWrite, uint64_t& size)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &data))
    {
        std::error_code ec;
        attrs  = 0;
        size   = fs::file_size(p, ec);
        GetSystemTimeAsFileTime(&ftWrite);
        return false;
    }
    attrs = data.dwFileAttributes;
    ftWrite = data.ftLastWriteTime;
    ULARGE_INTEGER ul2{};
    ul2.LowPart  = data.nFileSizeLow;
    ul2.HighPart = data.nFileSizeHigh;
    size = ul2.QuadPart;
    return true;
}

static fs::path GetExeDir()
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return fs::path();
    fs::path exePath(buf);
    return exePath.parent_path();
}

// ── Constructor ──────────────────────────────────────────────────

FileBrowser::FileBrowser()
{
    RefreshDrives();
    InitQuickAccess();
    LoadConfig();

    if (!m_quickAccess.empty())
        m_currentPath = m_quickAccess[0].path;
    else if (!m_drives.empty())
        m_currentPath = m_drives[0].rootPath;
    else
        m_currentPath = "C:\\";
    RefreshCurrentDir();
}

// ── Show hidden files setter ─────────────────────────────────────

void FileBrowser::SetShowHidden(bool show)
{
    m_showHidden = show;
    SaveConfig();
    RefreshCurrentDir();
}

// ── Config (INI format) ──────────────────────────────────────────

void FileBrowser::LoadConfig()
{
    fs::path iniPath = GetExeDir() / "FileTools.ini";
    std::ifstream f(iniPath.string());
    if (!f.is_open()) return;

    std::string line, section;
    while (std::getline(f, line))
    {
        size_t start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r");
        line = line.substr(start, end - start + 1);
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
        size_t ke = key.find_last_not_of(" \t");
        if (ke != std::string::npos) key = key.substr(0, ke + 1);
        size_t vs = val.find_first_not_of(" \t");
        if (vs != std::string::npos) val = val.substr(vs);

        if (section == "Settings" && key == "ShowHiddenFiles")
            m_showHidden = (val == "1" || val == "true" || val == "yes");
    }
}

void FileBrowser::SaveConfig()
{
    fs::path iniPath = GetExeDir() / "FileTools.ini";
    std::vector<std::string> lines;
    bool hasSettingsSection = false;
    bool keyWritten = false;

    {
        std::ifstream f(iniPath.string());
        if (f.is_open())
        {
            std::string line, section;
            while (std::getline(f, line))
            {
                size_t start = line.find_first_not_of(" \t\r");
                std::string trimmed = (start == std::string::npos) ? "" : line.substr(start);
                size_t e = trimmed.find_last_not_of(" \t\r");
                if (e != std::string::npos) trimmed = trimmed.substr(0, e + 1);

                if (!trimmed.empty() && trimmed[0] == '[' && trimmed.back() == ']')
                {
                    if (section == "Settings" && !keyWritten)
                    {
                        lines.push_back("ShowHiddenFiles=" + std::string(m_showHidden ? "1" : "0"));
                        keyWritten = true;
                    }
                    section = trimmed.substr(1, trimmed.size() - 2);
                    if (section == "Settings") hasSettingsSection = true;
                }
                else if (section == "Settings" && !keyWritten)
                {
                    size_t eq = trimmed.find('=');
                    if (eq != std::string::npos)
                    {
                        std::string k = trimmed.substr(0, eq);
                        size_t ke2 = k.find_last_not_of(" \t");
                        if (ke2 != std::string::npos) k = k.substr(0, ke2 + 1);
                        if (k == "ShowHiddenFiles")
                        {
                            lines.push_back("ShowHiddenFiles=" + std::string(m_showHidden ? "1" : "0"));
                            keyWritten = true;
                            continue;
                        }
                    }
                }
                lines.push_back(line);
            }
        }
    }

    if (!hasSettingsSection)
    {
        if (!lines.empty() && !lines.back().empty())
            lines.push_back("");
        lines.push_back("[Settings]");
    }
    if (!keyWritten)
        lines.push_back("ShowHiddenFiles=" + std::string(m_showHidden ? "1" : "0"));

    std::ofstream f(iniPath.string());
    if (!f.is_open()) return;
    for (size_t i = 0; i < lines.size(); ++i)
        f << lines[i] << "\n";
}

// ── Drive enumeration ────────────────────────────────────────────

void FileBrowser::RefreshDrives()
{
    m_drives.clear();

    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
    {
        if (!(mask & (1 << i))) continue;

        wchar_t letter = L'A' + i;
        wchar_t root[4] = {letter, L':', L'\\', L'\0'};

        UINT dt = GetDriveTypeW(root);

        std::wstring label = root;
        if (dt == DRIVE_FIXED)
            label = L"本地磁盘 (" + std::wstring(1, letter) + L":)";
        else if (dt == DRIVE_REMOVABLE)
            label = L"可移动磁盘 (" + std::wstring(1, letter) + L":)";
        else if (dt == DRIVE_CDROM)
            label = L"光盘 (" + std::wstring(1, letter) + L":)";
        else if (dt == DRIVE_REMOTE)
            label = L"网络驱动器 (" + std::wstring(1, letter) + L":)";
        else
            label = std::wstring(1, letter) + L":\\";

        wchar_t volName[128] = {};
        wchar_t fsName[128]  = {};
        DWORD serial = 0, maxLen = 0, flags = 0;
        if (GetVolumeInformationW(root, volName, 128, &serial, &maxLen, &flags, fsName, 128) && volName[0])
            label = std::wstring(volName) + L" (" + std::wstring(1, letter) + L":)";

        m_drives.push_back({letter, WstrToUtf8(root), WstrToUtf8(label), dt});
    }

    std::sort(m_drives.begin(), m_drives.end(), [](const DriveInfo& a, const DriveInfo& b) {
        auto priority = [](UINT t) {
            if (t == DRIVE_FIXED) return 0;
            if (t == DRIVE_REMOVABLE) return 1;
            if (t == DRIVE_REMOTE) return 2;
            return 3;
        };
        int pa = priority(a.driveType);
        int pb = priority(b.driveType);
        if (pa != pb) return pa < pb;
        return a.letter < b.letter;
    });
}

void FileBrowser::InitQuickAccess()
{
    m_quickAccess.clear();

    wchar_t buf[MAX_PATH];

    // Desktop
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOP, nullptr, 0, buf)))
        m_quickAccess.push_back({"桌面", fs::path(buf), "\xEE\xA2\xB1"});

    // Documents
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, buf)))
        m_quickAccess.push_back({"文档", fs::path(buf), "\xEE\xA2\xA5"});

    // Downloads
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROFILE, nullptr, 0, buf)))
    {
        fs::path downloads = fs::path(buf) / "Downloads";
        if (fs::exists(downloads))
            m_quickAccess.push_back({"下载", downloads, "\xEE\xA2\x96"});
    }
}

// ── Navigation ───────────────────────────────────────────────────

void FileBrowser::NavigateTo(const fs::path& path)
{
    if (path.empty() || !fs::exists(path)) return;

    if (!m_currentPath.empty() && !fs::equivalent(path, m_currentPath))
    {
        m_historyBack.push_back(m_currentPath);
        m_historyForward.clear();
    }

    m_currentPath = path;
    m_selectedPath.clear();
    m_selectedPaths.clear();
    m_anchorIndex = -1;

    // Release per-file icons from previous directory
    m_iconCache.ClearPerFileIcons();

    RefreshCurrentDir();
}

void FileBrowser::GoBack()
{
    if (m_historyBack.empty()) return;
    m_historyForward.push_back(m_currentPath);
    m_currentPath = m_historyBack.back();
    m_historyBack.pop_back();
    m_selectedPath.clear();
    m_selectedPaths.clear();
    m_anchorIndex = -1;
    m_iconCache.ClearPerFileIcons();
    RefreshCurrentDir();
}

void FileBrowser::GoForward()
{
    if (m_historyForward.empty()) return;
    m_historyBack.push_back(m_currentPath);
    m_currentPath = m_historyForward.back();
    m_historyForward.pop_back();
    m_selectedPath.clear();
    m_selectedPaths.clear();
    m_anchorIndex = -1;
    m_iconCache.ClearPerFileIcons();
    RefreshCurrentDir();
}

void FileBrowser::GoUp()
{
    if (m_currentPath.has_parent_path())
    {
        fs::path parent = m_currentPath.parent_path();
        if (parent != m_currentPath)
            NavigateTo(parent);
    }
}

// ── Directory listing ────────────────────────────────────────────

void FileBrowser::RefreshCurrentDir()
{
    m_entries.clear();

    if (m_currentPath.empty()) return;

    std::error_code ec;

    for (auto it = fs::directory_iterator(m_currentPath,
         fs::directory_options::skip_permission_denied, ec);
         it != fs::directory_iterator(); ++it)
    {
        FileEntry entry;
        entry.path = it->path();

        try {
            entry.name = entry.path.filename().u8string();
            if (entry.name.empty()) continue;

            entry.isDirectory = it->is_directory(ec);
            entry.isSymlink   = it->is_symlink(ec);

            if (!entry.isDirectory)
            {
                entry.fileSize = it->file_size(ec);
                if (ec) entry.fileSize = 0; ec.clear();
            }

            DWORD attrs = 0;
            FILETIME ftWrite{};
            uint64_t fsize = 0;
            GetFileAttributesExWide(entry.path, attrs, ftWrite, fsize);

            entry.lastWriteTime = FileTimeToUint64(ftWrite);
            if (!entry.isDirectory && fsize > 0)
                entry.fileSize = fsize;
            entry.isHidden = (attrs & FILE_ATTRIBUTE_HIDDEN) != 0;

            entry.extension = entry.path.extension().string();
            for (auto& c : entry.extension) c = (char)std::tolower((unsigned char)c);

            m_entries.push_back(entry);
        }
        catch (...) {}
    }
}

// ── Rendering ────────────────────────────────────────────────────

bool FileBrowser::Render()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

    bool open = true;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    if (!ImGui::Begin("文件浏览器", &open, flags))
    {
        ImGui::End();
        ImGui::PopStyleVar();
        return open;
    }

    RenderDriveBar();
    ImGui::Spacing();
    RenderPathBar();
    ImGui::Spacing();
    RenderFileTable();

    ImGui::End();
    ImGui::PopStyleVar();
    return open;
}

// ── Drive bar ────────────────────────────────────────────────────

void FileBrowser::RenderDriveBar()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Surface);

    float availableWidth = ImGui::GetContentRegionAvail().x;
    float btnWidth = ImGui::CalcTextSize("  本地磁盘 (Z:)  ").x;
    int btnsPerRow = std::max(1, (int)(availableWidth / (btnWidth + 4.0f)));

    int idx = 0;

    // Quick access
    for (const auto& qa : m_quickAccess)
    {
        if (idx > 0 && idx % btnsPerRow != 0)
            ImGui::SameLine();

        bool isCurrent = false;
        try {
            if (!m_currentPath.empty() && fs::exists(qa.path) && fs::exists(m_currentPath))
                isCurrent = fs::equivalent(qa.path, m_currentPath);
        } catch (...) {}

        if (isCurrent)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::AccentH);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::AccentH);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Surface);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::SurfaceH);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Border);
        }

        std::string label = std::string("  ") + qa.icon + " " + qa.name;
        if (ImGui::Button(label.c_str()))
            NavigateTo(qa.path);

        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", qa.path.u8string().c_str());

        ++idx;
    }

    // Separator
    if (!m_quickAccess.empty() && !m_drives.empty())
    {
        if (idx % btnsPerRow != 0)
            ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        ImGui::TextUnformatted(" | ");
        ImGui::PopStyleColor();
        if (idx % btnsPerRow != 0)
            idx = (idx / btnsPerRow) * btnsPerRow + btnsPerRow;
    }

    // Drives
    for (const auto& drv : m_drives)
    {
        if (idx > 0 && idx % btnsPerRow != 0)
            ImGui::SameLine();

        bool isCurrent = false;
        if (!m_currentPath.empty())
        {
            std::string curRoot = m_currentPath.root_path().u8string();
            isCurrent = (curRoot == drv.rootPath);
        }

        std::string label = drv.displayName;

        if (isCurrent)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::AccentH);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::AccentH);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button, Theme::Surface);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::SurfaceH);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, Theme::Border);
        }

        if (ImGui::Button(label.c_str()))
            NavigateTo(drv.rootPath);

        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", drv.rootPath.c_str());

        ++idx;
    }

    ImGui::PopStyleColor();
}

// ── Path bar (breadcrumb) ────────────────────────────────────────

void FileBrowser::RenderPathBar()
{
    float btnSz = ImGui::GetFrameHeight();

    // Back
    if (!m_historyBack.empty())
    {
        if (ImGui::ArrowButton("##back", ImGuiDir_Left)) GoBack();
    }
    else { ImGui::BeginDisabled(); ImGui::ArrowButton("##back", ImGuiDir_Left); ImGui::EndDisabled(); }
    ImGui::SetItemTooltip("后退");
    ImGui::SameLine();

    // Forward
    if (!m_historyForward.empty())
    {
        if (ImGui::ArrowButton("##fwd", ImGuiDir_Right)) GoForward();
    }
    else { ImGui::BeginDisabled(); ImGui::ArrowButton("##fwd", ImGuiDir_Right); ImGui::EndDisabled(); }
    ImGui::SetItemTooltip("前进");
    ImGui::SameLine();

    // Up
    if (m_currentPath.has_parent_path() && m_currentPath != m_currentPath.root_path())
    {
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) GoUp();
    }
    else { ImGui::BeginDisabled(); ImGui::ArrowButton("##up", ImGuiDir_Up); ImGui::EndDisabled(); }
    ImGui::SetItemTooltip("向上");
    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // ── Breadcrumb ──────────────────────────────────────────
    float availAfterCrumbs = 220.0f;
    float maxCrumbWidth = ImGui::GetContentRegionAvail().x - availAfterCrumbs;
    if (maxCrumbWidth < 100.0f) maxCrumbWidth = 100.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 4));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BgAlt);

    if (ImGui::BeginChild("##breadcrumb", ImVec2(maxCrumbWidth, btnSz + 4), ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
    {
        float crumbAvail = ImGui::GetContentRegionAvail().x;
        float cursorX = 0.0f;

        // Build segments
        std::vector<std::pair<std::string, fs::path>> segments;
        fs::path root = m_currentPath.root_path();
        segments.push_back({root.u8string(), root});

        fs::path accumulated = root;
        for (const auto& part : m_currentPath.relative_path())
        {
            accumulated /= part;
            segments.push_back({part.u8string(), accumulated});
        }

        for (size_t i = 0; i < segments.size(); ++i)
        {
            const auto& seg = segments[i];
            bool isLast = (i == segments.size() - 1);

            if (i > 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
                float arrowW = ImGui::CalcTextSize(" > ").x;
                ImGui::SetCursorPosX(cursorX);
                ImGui::TextUnformatted(">");
                ImGui::PopStyleColor();
                cursorX += arrowW;
                ImGui::SameLine();
            }

            std::string label = seg.first;
            if (label.empty()) label = "?";

            float segW = ImGui::CalcTextSize(label.c_str()).x + 8;

            // Overflow → show "..." then jump to last segment
            if (cursorX + segW > crumbAvail - 50 && !isLast && i < segments.size() - 1)
            {
                ImGui::SetCursorPosX(cursorX);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::SurfaceH);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Accent);
                if (ImGui::SmallButton("..."))
                    NavigateTo(segments[segments.size() - 2].second);
                ImGui::PopStyleColor(3);
                cursorX += ImGui::CalcTextSize("...").x + 12;
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
                ImGui::SetCursorPosX(cursorX);
                ImGui::TextUnformatted(">");
                ImGui::PopStyleColor();
                cursorX += ImGui::CalcTextSize(" > ").x;
                ImGui::SameLine();

                i = segments.size() - 2;
                continue;
            }

            ImGui::SetCursorPosX(cursorX);

            if (isLast)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Text);
                ImGui::TextUnformatted(label.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::SurfaceH);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Accent);
                if (ImGui::SmallButton(label.c_str()))
                    NavigateTo(seg.second);
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", seg.second.u8string().c_str());
            }

            cursorX += segW + 4;
            if (!isLast) ImGui::SameLine();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Hidden files checkbox
    ImGui::SameLine();
    {
        float checkW = ImGui::CalcTextSize(" 显示隐藏 ").x + 30;
        float availNow = ImGui::GetContentRegionAvail().x;
        if (availNow > checkW + 50)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availNow - checkW - 50));
    }
    if (ImGui::Checkbox("显示隐藏", &m_showHidden))
    {
        SaveConfig();
        RefreshCurrentDir();
    }
    ImGui::SetItemTooltip("显示/隐藏隐藏文件和文件夹");

    // Refresh
    ImGui::SameLine();
    if (ImGui::Button("刷新"))
        RefreshCurrentDir();
    ImGui::SetItemTooltip("刷新当前文件夹");
}

// ── Selection helpers ─────────────────────────────────────────────

bool FileBrowser::IsPathSelected(const fs::path& p) const
{
    for (const auto& sp : m_selectedPaths)
        if (sp == p) return true;
    return false;
}

void FileBrowser::SelectSingle(const fs::path& p)
{
    m_selectedPaths.clear();
    m_selectedPaths.push_back(p);
    m_selectedPath = p;
}

void FileBrowser::ToggleSelect(const fs::path& p)
{
    for (auto it = m_selectedPaths.begin(); it != m_selectedPaths.end(); ++it)
    {
        if (*it == p)
        {
            m_selectedPaths.erase(it);
            if (m_selectedPath == p)
                m_selectedPath = m_selectedPaths.empty() ? fs::path() : m_selectedPaths.back();
            return;
        }
    }
    m_selectedPaths.push_back(p);
    m_selectedPath = p;
}

void FileBrowser::SelectRange(int fromIdx, int toIdx, const std::vector<const FileEntry*>& visible)
{
    if (fromIdx < 0 || toIdx < 0) return;
    if (fromIdx > toIdx) std::swap(fromIdx, toIdx);

    m_selectedPaths.clear();
    for (int i = fromIdx; i <= toIdx && i < (int)visible.size(); ++i)
        m_selectedPaths.push_back(visible[i]->path);

    if (!m_selectedPaths.empty())
        m_selectedPath = m_selectedPaths.back();
}

void FileBrowser::ClearSelection()
{
    m_selectedPaths.clear();
    m_selectedPath.clear();
    m_anchorIndex = -1;
}

// ── File table ───────────────────────────────────────────────────

void FileBrowser::RenderFileTable()
{
    float availH = ImGui::GetContentRegionAvail().y;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::BgAlt);

    if (ImGui::BeginChild("##fileArea", ImVec2(0, availH), true, ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6, 3));

        ImGuiTableFlags tblFlags =
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_NoBordersInBody;

        if (ImGui::BeginTable("##fileTable", 4, tblFlags, ImVec2(0, 0)))
        {
            ImGui::TableSetupColumn("名称",
                ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch, 300);
            ImGui::TableSetupColumn("修改日期",
                ImGuiTableColumnFlags_WidthFixed, 160);
            ImGui::TableSetupColumn("类型",
                ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableSetupColumn("大小",
                ImGuiTableColumnFlags_WidthFixed, 100);
            ImGui::TableHeadersRow();

            // Build filtered visible list
            std::vector<const FileEntry*> visible;
            for (const auto& entry : m_entries)
            {
                if (!m_showHidden && entry.isHidden) continue;
                visible.push_back(&entry);
            }

            // Sort: directories first, then files, alphabetically within each group
            std::sort(visible.begin(), visible.end(), [](const FileEntry* a, const FileEntry* b) {
                if (a->isDirectory != b->isDirectory)
                    return a->isDirectory > b->isDirectory; // dirs first
                // Case-insensitive name comparison
                std::string na = a->name, nb = b->name;
                for (auto& c : na) c = (char)std::tolower((unsigned char)c);
                for (auto& c : nb) c = (char)std::tolower((unsigned char)c);
                return na < nb;
            });

            // Re-compute anchor: find which visible index holds the current anchor path
            // If anchor is stale (-1), it stays -1
            if (m_anchorIndex >= 0 && m_anchorIndex < (int)visible.size())
            {
                // Anchor is still valid since we use the visible index
            }
            else
            {
                m_anchorIndex = -1; // stale
            }

            bool ctrlDown  = ImGui::GetIO().KeyCtrl;
            bool shiftDown = ImGui::GetIO().KeyShift;

            for (int rowIdx = 0; rowIdx < (int)visible.size(); ++rowIdx)
            {
                const FileEntry& entry = *visible[rowIdx];
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);

                bool isSelected = IsPathSelected(entry.path);

                // ── Name column ──────────────────────────
                ImGui::TableSetColumnIndex(0);

                // Row highlight for selected items
                if (isSelected)
                    ImGui::PushStyleColor(ImGuiCol_Header, Theme::Accent);

                // Icon: use system icon for files, folder icon for dirs
                ImTextureID iconTex = entry.isDirectory
                    ? m_iconCache.GetFolderIcon()
                    : m_iconCache.GetFileIcon(entry.path);

                // Build display: icon (MDL2 or image) + name
                std::string display;
                if (iconTex)
                {
                    // Use system icon — 4 spaces to clear the 16px icon
                    display = entry.name;
                }
                else
                {
                    // Fallback to MDL2 icons
                    const char* icon = entry.isDirectory
                        ? Theme::IconFolder()
                        : Theme::IconForFile(entry.extension);
                    display = std::string("  ") + icon + "  " + entry.name;
                }

                if (entry.isDirectory)
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Folder);
                else
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::ColorForFile(entry.extension));

                // Leave room for the system icon image
                float _origCursorX = ImGui::GetCursorPosX();
                if (iconTex) ImGui::SetCursorPosX(_origCursorX + IconCache::IconSize + 6.0f);

                ImGui::Selectable(display.c_str(), isSelected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);

                ImGui::PopStyleColor();
                if (isSelected)
                    ImGui::PopStyleColor();

                // ── Click handling ────────────────────────
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    if (ctrlDown)
                    {
                        // Ctrl+Click: toggle
                        ToggleSelect(entry.path);
                        m_anchorIndex = rowIdx;
                    }
                    else if (shiftDown && m_anchorIndex >= 0)
                    {
                        // Shift+Click: range select
                        SelectRange(m_anchorIndex, rowIdx, visible);
                    }
                    else
                    {
                        // Normal click: single select
                        SelectSingle(entry.path);
                        m_anchorIndex = rowIdx;
                    }
                }

                // Double-click: directory → navigate; file → add to ops
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    if (entry.isDirectory)
                    {
                        NavigateTo(entry.path);
                        break;
                    }
                    else
                    {
                        if (m_addFilesCb)
                        {
                            // Add all selected files, or just this one if not selected
                            if (IsPathSelected(entry.path) && m_selectedPaths.size() > 1)
                                m_addFilesCb(m_selectedPaths);
                            else
                                m_addFilesCb({entry.path});
                        }
                    }
                }

                // Right-click
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                {
                    // If clicking on an unselected file, clear selection and select it
                    if (!IsPathSelected(entry.path))
                    {
                        SelectSingle(entry.path);
                        m_anchorIndex = rowIdx;
                    }
                    else
                    {
                        m_selectedPath = entry.path;
                    }
                    ImGui::OpenPopup("##fileCtxMenu");
                }

                // ── Render icon image if available ────────
                // (Draw the icon overlaid on the selectable area)
                if (iconTex)
                {
                    ImVec2 itemMin = ImGui::GetItemRectMin();
                    float iconY = itemMin.y + (ImGui::GetItemRectSize().y - IconCache::IconSize) * 0.5f;
                    ImGui::GetWindowDrawList()->AddImage(
                        iconTex,
                        ImVec2(itemMin.x + 2, iconY),
                        ImVec2(itemMin.x + 4 + IconCache::IconSize, iconY + IconCache::IconSize));
                }

                // ── Date column ──────────────────────────
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(FormatTime(entry.lastWriteTime).c_str());

                // ── Type column ──────────────────────────
                ImGui::TableSetColumnIndex(2);
                if (entry.isDirectory)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Folder);
                    ImGui::TextUnformatted("文件夹");
                    ImGui::PopStyleColor();
                }
                else
                {
                    std::string typeStr = entry.extension.empty() ? "文件" : entry.extension.substr(1) + " 文件";
                    ImGui::TextUnformatted(typeStr.c_str());
                }

                // ── Size column ──────────────────────────
                ImGui::TableSetColumnIndex(3);
                if (entry.isDirectory)
                    ImGui::TextUnformatted("");
                else
                    ImGui::TextUnformatted(FormatSize(entry.fileSize).c_str());
            }

            // ── Selection summary line ──────────────────
            if (!m_selectedPaths.empty())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
                ImGui::Text("%zu 个文件/文件夹已选择", m_selectedPaths.size());
                ImGui::PopStyleColor();
            }

            RenderContextMenu();
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ── Context menu ─────────────────────────────────────────────────

void FileBrowser::RenderContextMenu()
{
    if (ImGui::BeginPopup("##fileCtxMenu"))
    {
        std::vector<fs::path> toAdd;
        std::vector<fs::path> dirsToAdd;

        // Collect selected items
        if (m_selectedPaths.size() > 1)
        {
            for (const auto& p : m_selectedPaths)
                toAdd.push_back(p);
        }
        else if (!m_selectedPath.empty())
        {
            toAdd.push_back(m_selectedPath);
        }

        // Split into files vs directories
        std::vector<fs::path> filesOnly, dirsOnly;
        for (const auto& p : toAdd)
        {
            bool isDir = false;
            try { isDir = fs::is_directory(p); } catch (...) {}
            if (isDir) dirsOnly.push_back(p);
            else       filesOnly.push_back(p);
        }
        std::sort(filesOnly.begin(), filesOnly.end());
        filesOnly.erase(std::unique(filesOnly.begin(), filesOnly.end()), filesOnly.end());
        std::sort(dirsOnly.begin(), dirsOnly.end());
        dirsOnly.erase(std::unique(dirsOnly.begin(), dirsOnly.end()), dirsOnly.end());

        // ── Add files ────────────────────────────────────
        std::string fileLabel = "添加到操作列表";
        if (!filesOnly.empty())
            fileLabel += " (" + std::to_string(filesOnly.size()) + " 个文件)";

        if (ImGui::MenuItem(fileLabel.c_str(), nullptr, false, !filesOnly.empty()))
        {
            if (m_addFilesCb) m_addFilesCb(filesOnly);
        }

        // ── Add folders ──────────────────────────────────
        if (!dirsOnly.empty())
        {
            std::string dirLabel = "添加文件夹到操作列表";
            if (dirsOnly.size() == 1)
                dirLabel += " (" + dirsOnly[0].filename().string() + ")";
            else
                dirLabel += " (" + std::to_string(dirsOnly.size()) + " 个)";

            if (ImGui::MenuItem(dirLabel.c_str()))
            {
                if (m_addFilesCb) m_addFilesCb(dirsOnly);
            }
        }

        // ── Add all files inside folders (recursive) ─────
        if (!dirsOnly.empty())
        {
            std::string recLabel = "添加文件夹内所有文件（递归）";
            if (dirsOnly.size() == 1)
                recLabel += " (" + dirsOnly[0].filename().string() + ")";

            if (ImGui::MenuItem(recLabel.c_str()))
            {
                std::vector<fs::path> allFiles;
                for (const auto& dir : dirsOnly)
                {
                    try {
                        for (auto it = fs::recursive_directory_iterator(dir,
                             fs::directory_options::skip_permission_denied);
                             it != fs::recursive_directory_iterator(); ++it)
                        {
                            if (!it->is_directory())
                                allFiles.push_back(it->path());
                        }
                    } catch (...) {}
                }
                if (m_addFilesCb && !allFiles.empty())
                    m_addFilesCb(allFiles);
            }
        }

        if (!m_selectedPath.empty())
        {
            if (ImGui::MenuItem("在资源管理器中打开"))
            {
                std::wstring path = m_selectedPath.wstring();
                bool isDir = false;
                try { isDir = fs::is_directory(m_selectedPath); }
                catch (...) { isDir = false; }

                if (isDir)
                    ShellExecuteW(nullptr, L"open", L"explorer", path.c_str(), nullptr, SW_SHOW);
                else
                {
                    std::wstring cmd = L"/select,\"" + path + L"\"";
                    ShellExecuteW(nullptr, L"open", L"explorer", cmd.c_str(), nullptr, SW_SHOW);
                }
            }
        }

        ImGui::EndPopup();
    }
}

// ── Helpers ──────────────────────────────────────────────────────

std::string FileBrowser::FormatSize(uintmax_t bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIdx = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024.0 && unitIdx < 4)
    {
        size /= 1024.0;
        ++unitIdx;
    }

    std::ostringstream oss;
    if (unitIdx == 0)
        oss << bytes << " " << units[unitIdx];
    else
        oss << std::fixed << std::setprecision(1) << size << " " << units[unitIdx];
    return oss.str();
}

std::string FileBrowser::FormatTime(uint64_t filetime)
{
    if (filetime == 0) return "";

    FILETIME ft;
    ft.dwLowDateTime  = static_cast<DWORD>(filetime & 0xFFFFFFFF);
    ft.dwHighDateTime = static_cast<DWORD>(filetime >> 32);

    SYSTEMTIME stUtc, stLocal;
    FileTimeToSystemTime(&ft, &stUtc);
    SystemTimeToTzSpecificLocalTime(nullptr, &stUtc, &stLocal);

    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d",
        stLocal.wYear, stLocal.wMonth, stLocal.wDay,
        stLocal.wHour, stLocal.wMinute);
    return buf;
}

std::string FileBrowser::PathToUtf8(const fs::path& p) const
{
    return p.u8string();
}

bool FileBrowser::HasChildren(const fs::path& dir)
{
    std::error_code ec;
    auto it = fs::directory_iterator(dir, ec);
    return it != fs::directory_iterator();
}
