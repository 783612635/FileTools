#pragma once
#include <filesystem>
#include <vector>
#include <string>
#include <functional>

namespace fs = std::filesystem;

// ── Archive extraction / compression utilities ─────────────────────
namespace ArchiveOps
{

// Per-file result
struct ArchiveResult
{
    bool        success    = false;
    std::string sourcePath;       // UTF-8
    std::string destPath;         // UTF-8
    std::string errorMsg;
};

// Overall batch result
struct BatchArchiveResult
{
    std::vector<ArchiveResult> results;
    int  totalFiles = 0;
    int  okFiles    = 0;
    int  failFiles  = 0;

    bool AllOk()     const { return failFiles == 0; }
    bool SomeOk()    const { return okFiles > 0; }
};

// Progress callback:  current / total,  description text
using ProgressCb = std::function<void(int current, int total, const std::string& desc)>;

// ── 7z ──────────────────────────────────────────────────────────
// Requires 7za.exe next to the executable.

// Extract a 7z archive to outDir.  Creates outDir if needed.
BatchArchiveResult Extract7z(const fs::path& archivePath,
                              const fs::path& outDir,
                              ProgressCb progress = {});

// Compress files into a .7z archive.
BatchArchiveResult Compress7z(const std::vector<fs::path>& paths,
                               const fs::path& archivePath,
                               ProgressCb progress = {});

// ── Zip (Windows COM Shell fallback) ────────────────────────────
// Used when 7z fails — handles zip files that 7z can't parse
// (e.g. files disguised as .mp4 that are actually ZIP).

BatchArchiveResult ExtractZip(const fs::path& archivePath,
                               const fs::path& outDir,
                               ProgressCb progress = {});

// ── Auto-detect ─────────────────────────────────────────────────
// Try 7z first; if that fails, try zip.
BatchArchiveResult ExtractAuto(const fs::path& archivePath,
                                const fs::path& outDir,
                                ProgressCb progress = {});

} // namespace ArchiveOps
