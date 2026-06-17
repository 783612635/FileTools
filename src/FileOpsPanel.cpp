#include "FileOpsPanel.h"
#include "FileOperations.h"
#include "ArchiveOps.h"
#include "Theme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <Windows.h>
#include <shlobj.h>
#include <unordered_set>

// ── Static helper: archive file detection ────────────────────────

static bool IsArchiveFile(const fs::path& p)
{
    std::string ext = p.extension().string();
    // Lowercase
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    static const std::unordered_set<std::string> archiveExts = {
        ".7z", ".zip", ".rar", ".tar", ".gz", ".bz2", ".xz",
        ".tgz", ".tbz2", ".txz", ".iso", ".cab", ".arj", ".lzh",
    };
    return archiveExts.find(ext) != archiveExts.end();
}

// ── Static helper: Windows folder picker ─────────────────────────

static std::string PickFolderWin32()
{
    IFileOpenDialog* pDlg = nullptr;
    std::string result;

    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                   IID_IFileOpenDialog, (void**)&pDlg);
    if (FAILED(hr)) return {};

    DWORD flags;
    pDlg->GetOptions(&flags);
    pDlg->SetOptions(flags | FOS_PICKFOLDERS);
    pDlg->SetTitle(L"选择目标文件夹");

    hr = pDlg->Show(nullptr);
    if (SUCCEEDED(hr))
    {
        IShellItem* pItem = nullptr;
        hr = pDlg->GetResult(&pItem);
        if (SUCCEEDED(hr))
        {
            PWSTR path = nullptr;
            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(hr) && path)
            {
                int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                result.resize(len > 0 ? len - 1 : 0);
                if (len > 1)
                    WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), len, nullptr, nullptr);
                CoTaskMemFree(path);
            }
            pItem->Release();
        }
    }
    pDlg->Release();
    return result;
}

// ── Constructor ──────────────────────────────────────────────────

FileOpsPanel::FileOpsPanel()
{
    m_targetFolder[0] = '\0';
}

// ── File management ──────────────────────────────────────────────

void FileOpsPanel::AddFiles(const std::vector<fs::path>& paths)
{
    for (const auto& p : paths)
    {
        if (p.empty()) continue;
        try { if (!fs::exists(p)) continue; } catch (...) { continue; }

        bool duplicate = false;
        for (const auto& existing : m_files)
        {
            if (existing == p) { duplicate = true; break; }
        }
        if (!duplicate)
            m_files.push_back(p);
    }
    m_statusMsg = "已添加 " + std::to_string(paths.size()) + " 个项目";
    m_statusTime = 3.0f;
    m_statusIsError = false;
}

void FileOpsPanel::AddFile(const fs::path& path)
{
    AddFiles({path});
}

void FileOpsPanel::RemoveSelected()
{
    // Collect indices to remove (sorted descending for safe removal)
    std::vector<size_t> toRemove(m_selectedIndices.begin(), m_selectedIndices.end());
    std::sort(toRemove.begin(), toRemove.end(), std::greater<size_t>());

    for (size_t idx : toRemove)
    {
        if (idx < m_files.size())
            m_files.erase(m_files.begin() + idx);
    }
    m_selectedIndices.clear();
    m_anchorIndex = -1;

    m_statusMsg = "已移除选中文件";
    m_statusTime = 2.0f;
    m_statusIsError = false;
}

void FileOpsPanel::ClearAll()
{
    m_files.clear();
    m_selectedIndices.clear();
    m_anchorIndex = -1;
    m_statusMsg = "列表已清空";
    m_statusTime = 2.0f;
    m_statusIsError = false;
}

// ── Selection helpers ─────────────────────────────────────────────

bool FileOpsPanel::IsIndexSelected(size_t i) const
{
    return m_selectedIndices.find(i) != m_selectedIndices.end();
}

void FileOpsPanel::SelectSingle(size_t i)
{
    m_selectedIndices.clear();
    m_selectedIndices.insert(i);
    m_anchorIndex = (int)i;
}

void FileOpsPanel::ToggleSelect(size_t i)
{
    auto it = m_selectedIndices.find(i);
    if (it != m_selectedIndices.end())
        m_selectedIndices.erase(it);
    else
        m_selectedIndices.insert(i);
    m_anchorIndex = (int)i;
}

void FileOpsPanel::SelectRange(int from, int to)
{
    if (from < 0 || to < 0) return;
    if (from > to) std::swap(from, to);

    for (int i = from; i <= to; ++i)
        m_selectedIndices.insert((size_t)i);
}

void FileOpsPanel::ClearSelection()
{
    m_selectedIndices.clear();
    m_anchorIndex = -1;
}

// ── Render ───────────────────────────────────────────────────────

bool FileOpsPanel::Render()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));

    bool open = true;
    if (!ImGui::Begin("文件操作列表", &open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        ImGui::PopStyleVar();
        return open;
    }

    // Header: file count + clear button
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextAcc);
    ImGui::Text("%zu 个文件待操作", m_files.size());
    ImGui::PopStyleColor();

    ImGui::SameLine();
    if (m_files.empty())
        ImGui::BeginDisabled();
    if (ImGui::Button("清空列表", ImVec2(80, 0)))
        ClearAll();
    ImGui::SetItemTooltip("移除列表中的所有文件");
    if (m_files.empty())
        ImGui::EndDisabled();

    // Remove selected button
    if (!m_selectedIndices.empty())
    {
        ImGui::SameLine();
        if (ImGui::Button("移除选中", ImVec2(80, 0)))
            RemoveSelected();
        ImGui::SetItemTooltip("移除选中的文件");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── File list ────────────────────────────────────
    RenderFileList();

    // ── Operation scope indicator ─────────────────────
    // Clearly tell the user what will be processed
    ImGui::Spacing();
    if (!m_files.empty())
    {
        if (!m_selectedIndices.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Warning);
            ImGui::Text("\xEE\x9E\xBA  已选择 %zu 个文件 — 下方操作仅应用于选中的文件", m_selectedIndices.size());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton("取消选择"))
                ClearSelection();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
            ImGui::Text("\xEE\x9C\x8E  未选择文件 — 下方操作默认应用于全部 %zu 个文件", m_files.size());
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();

    // ── Operation sections ───────────────────────────
    RenderRenameExt();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    RenderTrimChars();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    RenderMoveToFolder();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    RenderArchiveOps();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Status bar ───────────────────────────────────
    RenderStatusBar();

    ImGui::End();
    ImGui::PopStyleVar();
    return open;
}

// ── File list ────────────────────────────────────────────────────

void FileOpsPanel::RenderFileList()
{
    ImGui::Text("操作列表:");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::Text("(从右侧浏览器右键添加 | 不选=全部处理)");
    ImGui::PopStyleColor();

    float listH = std::min(200.0f, std::max(80.0f, m_files.size() * 24.0f + 8.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Theme::Bg);

    if (ImGui::BeginChild("##fileOpList", ImVec2(0, listH), true))
    {
        ImGui::SetItemTooltip("按住 Ctrl 点击 → 多选单个文件\n按住 Shift 点击 → 范围选择文件\n不选择任何文件时 → 操作默认处理全部文件");
        if (m_files.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
            ImGui::TextWrapped("还没有添加文件。\n在右侧文件浏览器中右键点击文件 → \"添加到操作列表\"");
            ImGui::PopStyleColor();
        }
        else
        {
            bool ctrlDown  = ImGui::GetIO().KeyCtrl;
            bool shiftDown = ImGui::GetIO().KeyShift;

            for (size_t i = 0; i < m_files.size(); ++i)
            {
                ImGui::PushID((int)i);

                bool isSelected = IsIndexSelected(i);

                // Row highlight for selected items
                if (isSelected)
                {
                    ImGui::PushStyleColor(ImGuiCol_Header, Theme::Accent);
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::AccentH);
                }
                else if (i % 2 == 0)
                {
                    ImGui::PushStyleColor(ImGuiCol_Header, Theme::BgAlt);
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::SurfaceH);
                }
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::SurfaceH);
                }

                bool sel = isSelected;
                ImGui::Selectable("##row", &sel,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, 22));

                ImGui::PopStyleColor(2);

                // ── Click handling ────────────────────────
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    if (ctrlDown)
                        ToggleSelect(i);
                    else if (shiftDown && m_anchorIndex >= 0)
                        SelectRange(m_anchorIndex, (int)i);
                    else if (isSelected && m_selectedIndices.size() <= 1)
                        ClearSelection();  // re-click single selected → deselect
                    else
                        SelectSingle(i);
                }

                // Double-click on unselected item → remove it
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
                    && !ctrlDown && !shiftDown && !isSelected)
                {
                    m_files.erase(m_files.begin() + i);
                    // Fix indices after removal
                    std::unordered_set<size_t> newSel;
                    for (auto s : m_selectedIndices)
                    {
                        if (s > i) newSel.insert(s - 1);
                        else if (s < i) newSel.insert(s);
                    }
                    m_selectedIndices = std::move(newSel);
                    if (m_anchorIndex > (int)i) m_anchorIndex--;
                    else if (m_anchorIndex == (int)i) m_anchorIndex = -1;
                    ImGui::PopID();
                    --i;
                    continue;
                }

                ImGui::SameLine();

                // Remove "X" button
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Theme::Error);
                ImGui::PushStyleColor(ImGuiCol_Text, Theme::Error);
                if (ImGui::SmallButton("X"))
                {
                    m_files.erase(m_files.begin() + i);
                    std::unordered_set<size_t> newSel;
                    for (auto s : m_selectedIndices)
                    {
                        if (s > i) newSel.insert(s - 1);
                        else if (s < i) newSel.insert(s);
                    }
                    m_selectedIndices = std::move(newSel);
                    if (m_anchorIndex > (int)i) m_anchorIndex--;
                    else if (m_anchorIndex == (int)i) m_anchorIndex = -1;
                    ImGui::PopID();
                    --i;
                    ImGui::PopStyleColor(3);
                    continue;
                }
                ImGui::PopStyleColor(3);

                ImGui::SameLine();

                // Filename — show folder icon for directories
                std::string name = m_files[i].filename().u8string();
                bool isDir = false;
                try { isDir = fs::is_directory(m_files[i]); } catch (...) {}
                if (isDir) name = std::string(Theme::IconFolder()) + " " + name;

                if (isSelected)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1));
                else if (isDir)
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Folder);
                ImGui::TextUnformatted(name.c_str());
                if (isSelected || isDir)
                    ImGui::PopStyleColor();

                // Tooltip with full path
                if (ImGui::IsItemHovered())
                {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(m_files[i].u8string().c_str());
                    ImGui::EndTooltip();
                }

                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (!m_selectedIndices.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        ImGui::Text("%zu 个文件已选择", m_selectedIndices.size());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("清除选择"))
            ClearSelection();
    }
}

// ── Rename Extension ─────────────────────────────────────────────

void FileOpsPanel::RenderRenameExt()
{
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextAcc);
    ImGui::TextUnformatted("修改后缀名");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(120);
    ImGui::InputTextWithHint("##newExt", ".txt", m_newExt, sizeof(m_newExt));
    ImGui::SetItemTooltip("输入新后缀名，如 .jpg, .png, .bak\n包含 . 号");
    ImGui::SameLine();

    bool hasFiles = !m_files.empty();
    if (!hasFiles)
        ImGui::BeginDisabled();

    if (ImGui::Button("执行重命名", ImVec2(100, 0)))
    {
        try
        {
            // Operate on selected files, or all if none selected
            std::vector<fs::path> targets;
            if (!m_selectedIndices.empty())
            {
                for (auto idx : m_selectedIndices)
                    if (idx < m_files.size()) targets.push_back(m_files[idx]);
            }
            else
            {
                targets = m_files;
            }

            auto result = FileOps::ChangeExtension(targets, std::string(m_newExt));
            ShowOperationResult("修改后缀名", result.successCount, result.failCount);

            for (auto& r : result.results)
            {
                if (r.success)
                {
                    for (auto& f : m_files)
                    {
                        if (f == r.sourcePath) { f = r.destPath; break; }
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            m_statusMsg = std::string("修改后缀名异常: ") + e.what();
            m_statusTime = 8.0f;
            m_statusIsError = true;
        }
        catch (...)
        {
            m_statusMsg = "修改后缀名发生未知异常";
            m_statusTime = 8.0f;
            m_statusIsError = true;
        }
    }
    ImGui::SetItemTooltip(!m_selectedIndices.empty()
        ? u8"仅重命名已选中的文件"
        : u8"重命名列表中的全部文件");

    if (!hasFiles)
        ImGui::EndDisabled();
}

// ── Trim Characters ──────────────────────────────────────────────

void FileOpsPanel::RenderTrimChars()
{
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextAcc);
    ImGui::TextUnformatted("删除文件名字符");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::InputInt("删除数量", &m_trimCount, 1, 1);
    if (m_trimCount < 1) m_trimCount = 1;
    if (m_trimCount > 255) m_trimCount = 255;
    ImGui::SetItemTooltip("要从文件名中删除的字符数量");

    ImGui::SameLine();
    ImGui::RadioButton("从前面删", &m_trimMode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("从后面删", &m_trimMode, 1);

    ImGui::Spacing();

    bool hasFiles = !m_files.empty();
    if (!hasFiles)
        ImGui::BeginDisabled();

    if (ImGui::Button("执行删除", ImVec2(100, 0)))
    {
        std::vector<fs::path> targets;
        if (!m_selectedIndices.empty())
        {
            for (auto idx : m_selectedIndices)
                if (idx < m_files.size()) targets.push_back(m_files[idx]);
        }
        else
        {
            targets = m_files;
        }

        auto result = FileOps::TrimChars(targets, m_trimCount, m_trimMode == 0);
        ShowOperationResult("删除字符", result.successCount, result.failCount);

        for (auto& r : result.results)
        {
            if (r.success)
            {
                for (auto& f : m_files)
                {
                    if (f == r.sourcePath) { f = r.destPath; break; }
                }
            }
        }
    }
    ImGui::SetItemTooltip(!m_selectedIndices.empty()
        ? u8"仅删除已选中文件的字符"
        : u8"删除全部文件名的字符");

    if (!hasFiles)
        ImGui::EndDisabled();
}

// ── Move to Folder ───────────────────────────────────────────────

void FileOpsPanel::RenderMoveToFolder()
{
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextAcc);
    ImGui::TextUnformatted("移动到文件夹");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    float availW = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(availW - 48);
    ImGui::InputTextWithHint("##targetFolder", "C:\\目标文件夹", m_targetFolder, sizeof(m_targetFolder));
    ImGui::SetItemTooltip("输入目标文件夹路径，或点击 [...] 浏览选择");

    ImGui::SameLine();

    if (ImGui::Button("...", ImVec2(28, 0)))
    {
        std::string picked = PickFolderWin32();
        if (!picked.empty())
        {
            strncpy(m_targetFolder, picked.c_str(), sizeof(m_targetFolder) - 1);
            m_targetFolder[sizeof(m_targetFolder) - 1] = '\0';
        }
    }
    ImGui::SetItemTooltip("浏览选择文件夹");

    ImGui::Spacing();

    bool hasFiles = !m_files.empty() && m_targetFolder[0] != '\0';
    if (!hasFiles)
        ImGui::BeginDisabled();

    if (ImGui::Button("执行移动", ImVec2(100, 0)))
    {
        std::vector<fs::path> targets;
        if (!m_selectedIndices.empty())
        {
            for (auto idx : m_selectedIndices)
                if (idx < m_files.size()) targets.push_back(m_files[idx]);
        }
        else
        {
            targets = m_files;
        }

        fs::path target(m_targetFolder);
        auto result = FileOps::MoveToFolder(targets, target);
        ShowOperationResult("移动文件", result.successCount, result.failCount);

        auto newEnd = std::remove_if(m_files.begin(), m_files.end(),
            [&](const fs::path& f) {
                for (auto& r : result.results)
                    if (r.success && f == r.sourcePath) return true;
                return false;
            });
        m_files.erase(newEnd, m_files.end());
        m_selectedIndices.clear();
        m_anchorIndex = -1;
    }
    ImGui::SetItemTooltip(!m_selectedIndices.empty()
        ? u8"仅移动已选中的文件"
        : u8"移动列表中的全部文件");

    if (!hasFiles)
        ImGui::EndDisabled();
}

// ── Archive Operations (7z / zip) ────────────────────────────────

void FileOpsPanel::RenderArchiveOps()
{
    // ── Extract section ────────────────────────────────────
    // Auto-detect archives; only show when archives exist
    // Default extract to same directory as the archive file

    std::vector<fs::path> archiveTargets;
    if (!m_selectedIndices.empty())
    {
        for (auto idx : m_selectedIndices)
        {
            if (idx < m_files.size() && IsArchiveFile(m_files[idx]))
                archiveTargets.push_back(m_files[idx]);
        }
    }
    else
    {
        for (const auto& f : m_files)
            if (IsArchiveFile(f))
                archiveTargets.push_back(f);
    }

    if (!archiveTargets.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextAcc);
        ImGui::TextUnformatted("解压");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        ImGui::Text("%zu 个压缩文件 — 解压到各自同级目录",
            archiveTargets.size());
        ImGui::PopStyleColor();
        ImGui::Spacing();

        CheckExtractDone(); // pick up result from async thread

        bool canExtract = !archiveTargets.empty() && !m_extracting;
        if (!canExtract)
            ImGui::BeginDisabled();

        if (ImGui::Button(m_extracting ? "解压中..." : "执行解压", ImVec2(100, 0)))
        {
            m_extracting = true;
            m_showExtractDone = false;
            m_extractResult.clear();
            if (m_extractThread.joinable()) m_extractThread.join();
            m_extractThread = std::thread(&FileOpsPanel::RunExtractAsync, this, archiveTargets);
        }
        ImGui::SetItemTooltip(u8"每个压缩文件解压到自身所在目录的同名文件夹中");

        if (!canExtract)
            ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    // ── Compress section ────────────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextAcc);
    ImGui::TextUnformatted("压缩");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
    ImGui::Text("每个文件单独压缩为同名 .7z，输出到同级目录");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ── Overwrite confirmation popup ─────────────────────
    if (m_showOverwriteAsk)
    {
        ImGui::OpenPopup("覆盖确认");
        m_showOverwriteAsk = false;
    }
    if (ImGui::BeginPopupModal("覆盖确认", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("部分 .7z 文件已存在，是否覆盖？");
        ImGui::Spacing();
        if (ImGui::Button("覆盖继续", ImVec2(100, 0)))
        {
            ImGui::CloseCurrentPopup();
            m_compressing = true; m_compressResult.clear();
            if (m_compressThread.joinable()) m_compressThread.join();
            m_compressThread = std::thread(&FileOpsPanel::RunCompressAsync, this, m_pendingCompressTargets);
        }
        ImGui::SameLine();
        if (ImGui::Button("取消", ImVec2(100, 0)))
        {
            ImGui::CloseCurrentPopup();
            m_pendingCompressTargets.clear();
        }
        ImGui::EndPopup();
    }

    // ── Compress button ───────────────────────────────────
    CheckCompressDone(); // pick up result from async thread
    bool canCompress = !m_files.empty() && !m_compressing;
    if (!canCompress)
        ImGui::BeginDisabled();

    if (ImGui::Button(m_compressing ? "压缩中..." : "压缩为 7z", ImVec2(120, 0)))
    {
        // Collect targets: selected or all
        std::vector<fs::path> targets;
        if (!m_selectedIndices.empty())
        {
            for (auto idx : m_selectedIndices)
                if (idx < m_files.size()) targets.push_back(m_files[idx]);
        }
        else
        {
            targets = m_files;
        }
        if (!targets.empty())
        {
            // Check for existing output files
            bool existing = false;
            try {
                for (const auto& p : targets)
                {
                    std::error_code ec;
                    bool isDir = fs::is_directory(p, ec);
                    fs::path outPath = isDir
                        ? p.parent_path() / (p.filename().string() + ".7z")
                        : fs::path(p).replace_extension(".7z");
                    if (fs::exists(outPath, ec)) { existing = true; break; }
                }
            } catch (...) { existing = false; }
            if (existing)
            {
                m_pendingCompressTargets = targets;
                m_showOverwriteAsk = true;
            }
            else
            {
                m_compressing = true;
                m_showCompressDone = false;
                m_compressResult.clear();
                if (m_compressThread.joinable()) m_compressThread.join();
                m_compressThread = std::thread(&FileOpsPanel::RunCompressAsync, this, targets);
            }
        }
    }
    ImGui::SetItemTooltip(!m_selectedIndices.empty()
        ? u8"仅压缩已选中的文件 — 每个文件单独压缩为同名 .7z"
        : u8"压缩列表中全部文件 — 每个文件单独压缩为同名 .7z");
    if (!canCompress)
        ImGui::EndDisabled();
}

// ── Async compression thread ─────────────────────────────────────

void FileOpsPanel::RunCompressAsync(std::vector<fs::path> targets)
{
    int ok = 0, fail = 0;
    for (const auto& p : targets)
    {
        try
        {
            std::error_code ec;
            if (fs::is_directory(p, ec))
            {
                std::vector<fs::path> allFiles;
                for (auto it = fs::recursive_directory_iterator(p,
                     fs::directory_options::skip_permission_denied);
                     it != fs::recursive_directory_iterator(); ++it)
                    if (!it->is_directory()) allFiles.push_back(it->path());
                if (allFiles.empty()) { ++fail; continue; }
                fs::path outPath = p.parent_path() / (p.filename().string() + ".7z");
                auto result = ArchiveOps::Compress7z(allFiles, outPath);
                ok += result.okFiles > 0 ? 1 : 0;
                fail += result.failFiles > 0 ? 1 : 0;
            }
            else
            {
                fs::path outPath = p;
                outPath.replace_extension(".7z");
                auto result = ArchiveOps::Compress7z({p}, outPath);
                ok += result.okFiles;
                fail += result.failFiles;
            }
        }
        catch (...) { ++fail; }
    }
    m_compressResult = ok > 0
        ? "\xEE\x9C\x8E 压缩完成 — 成功: " + std::to_string(ok) + ", 失败: " + std::to_string(fail)
        : "\xEE\x9D\x98 压缩失败 — " + std::to_string(fail) + " 个文件";
    m_compressOk = ok; m_compressFail = fail;
    m_showCompressDone = true;
    m_compressing = false;
}

void FileOpsPanel::CheckCompressDone()
{
    if (m_showCompressDone && !m_compressing)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, m_compressFail > 0 ? Theme::Warning : Theme::Success);
        ImGui::TextWrapped("%s", m_compressResult.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("清空列表"))
        {
            ClearAll();
            m_showCompressDone = false;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("知道了"))
            m_showCompressDone = false;
    }
}

// ── Async extraction thread ─────────────────────────────────────

void FileOpsPanel::RunExtractAsync(std::vector<fs::path> targets)
{
    int ok = 0, fail = 0;
    for (const auto& p : targets)
    {
        try
        {
            fs::path parentDir = p.parent_path();
            fs::path subDir = parentDir / p.stem();
            auto result = ArchiveOps::ExtractAuto(p, subDir);
            ok   += result.okFiles;
            fail += result.failFiles;
        }
        catch (...) { ++fail; }
    }
    m_extractResult = ok > 0
        ? "\xEE\x9C\x8E 解压完成 — 成功: " + std::to_string(ok) + ", 失败: " + std::to_string(fail)
        : "\xEE\x9D\x98 解压失败 — " + std::to_string(fail) + " 个文件";
    m_extractOk = ok; m_extractFail = fail;
    m_showExtractDone = true;
    m_extracting = false;
}

void FileOpsPanel::CheckExtractDone()
{
    if (m_showExtractDone && !m_extracting)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, m_extractFail > 0 ? Theme::Warning : Theme::Success);
        ImGui::TextWrapped("%s", m_extractResult.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("知道了"))
            m_showExtractDone = false;
    }
}

// ── Status bar ───────────────────────────────────────────────────

void FileOpsPanel::RenderStatusBar()
{
    float dt = ImGui::GetIO().DeltaTime;
    if (m_statusTime > 0.0f)
        m_statusTime -= dt;

    if (m_compressing || m_extracting)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Accent);
        ImGui::Text(m_compressing ? "正在压缩..." : "正在解压...");
        ImGui::PopStyleColor();
        return;
    }

    if (m_statusTime > 0.0f && !m_statusMsg.empty())
    {
        float alpha = 1.0f;
        if (m_statusTime < 1.0f)
            alpha = m_statusTime;

        ImVec4 color = m_statusIsError ? Theme::Error : Theme::Success;
        color.w *= alpha;

        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", m_statusMsg.c_str());
        ImGui::PopStyleColor();
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::TextDim);
        if (m_files.empty())
            ImGui::Text("提示：将文件添加到列表后即可执行批量操作");
        else
        {
            if (!m_selectedIndices.empty())
                ImGui::Text("就绪 — %zu 个文件待处理 (%zu 已选择)",
                    m_files.size(), m_selectedIndices.size());
            else
                ImGui::Text("就绪 — %zu 个文件待处理", m_files.size());
        }
        ImGui::PopStyleColor();
    }
}

// ── Show operation result ────────────────────────────────────────

void FileOpsPanel::ShowOperationResult(const std::string& opName, int ok, int fail)
{
    if (fail == 0)
    {
        m_statusMsg = opName + " 完成！成功: " + std::to_string(ok);
        m_statusIsError = false;
    }
    else
    {
        m_statusMsg = opName + " 完成 — 成功: " + std::to_string(ok) +
                      ", 失败: " + std::to_string(fail);
        m_statusIsError = true;
    }
    m_statusTime = 5.0f;
}
