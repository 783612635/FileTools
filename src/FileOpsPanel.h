#pragma once
#include "imgui.h"
#include <filesystem>
#include <vector>
#include <string>
#include <functional>
#include <unordered_set>
#include <thread>
#include <atomic>

namespace fs = std::filesystem;
// ── File Operations Panel (left dock) ────────────────────────────
class FileOpsPanel
{
public:
    FileOpsPanel();
    ~FileOpsPanel()
    {
        if (m_compressThread.joinable()) m_compressThread.detach();
        if (m_extractThread.joinable())  m_extractThread.detach();
    }

    void AddFiles(const std::vector<fs::path>& paths);
    void AddFile(const fs::path& path);
    void RemoveSelected();
    void ClearAll();

    bool Render();

private:
    void RenderFileList();
    void RenderRenameExt();
    void RenderTrimChars();
    void RenderMoveToFolder();
    void RenderArchiveOps();
    void RenderStatusBar();
    void ShowOperationResult(const std::string& opName, int ok, int fail);

    // Selection helpers
    bool IsIndexSelected(size_t i) const;
    void SelectSingle(size_t i);
    void ToggleSelect(size_t i);
    void SelectRange(int from, int to);
    void ClearSelection();

    // ── State ───────────────────────────────────────────────
    std::vector<fs::path>     m_files;
    std::unordered_set<size_t> m_selectedIndices;
    int                       m_anchorIndex = -1;

    char m_newExt[32] = ".txt";
    int  m_trimCount  = 1;
    int  m_trimMode   = 0;
    char m_targetFolder[512] = {};

    std::string m_statusMsg;
    float       m_statusTime   = 0.0f;
    bool        m_statusIsError = false;

    // Async compression
    std::thread       m_compressThread;
    std::atomic<bool> m_compressing{false};
    std::string       m_compressResult;
    int               m_compressOk = 0, m_compressFail = 0;
    bool              m_showCompressDone = false;
    bool              m_showOverwriteAsk = false;
    std::vector<fs::path> m_pendingCompressTargets;
    void RunCompressAsync(std::vector<fs::path> targets);
    void CheckCompressDone();

    // Async extraction
    std::thread       m_extractThread;
    std::atomic<bool> m_extracting{false};
    std::string       m_extractResult;
    int               m_extractOk = 0, m_extractFail = 0;
    bool              m_showExtractDone = false;
    void RunExtractAsync(std::vector<fs::path> targets);
    void CheckExtractDone();
};
