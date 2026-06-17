#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// ── Per-file operation result ────────────────────────────────────
struct OpResult
{
    fs::path sourcePath;
    fs::path destPath;
    bool     success = false;
    std::string errorMsg;
};

// ── Batch operation results ──────────────────────────────────────
struct BatchResult
{
    std::vector<OpResult> results;
    int  totalFiles   = 0;
    int  successCount = 0;
    int  failCount    = 0;

    bool AllSucceeded() const { return failCount == 0; }
    bool AnySucceeded() const { return successCount > 0; }
};

// ── Operation engine (all static, operates on filesystem) ────────
namespace FileOps
{

// Replace the extension of every file in `files` with `newExtension`.
// newExtension should include the dot, e.g. ".txt" or ".png"
// If newExtension is empty, the extension is removed.
BatchResult ChangeExtension(
    const std::vector<fs::path>& files,
    const std::string&           newExtension);

// Remove `count` characters from the front (fromFront=true) or
// back (fromFront=false) of each file's stem.
// The extension is never touched.
// If a stem would become empty, that file is skipped with an error.
BatchResult TrimChars(
    const std::vector<fs::path>& files,
    int                          count,
    bool                         fromFront);

// Move all files to `targetFolder`.
// Creates the folder if it doesn't exist.
// If a file already exists at the destination, a suffix "_(N)" is appended.
BatchResult MoveToFolder(
    const std::vector<fs::path>& files,
    const fs::path&              targetFolder);

} // namespace FileOps
