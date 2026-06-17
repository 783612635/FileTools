#include "FileOperations.h"
#include <Windows.h>
#include <algorithm>
#include <system_error>
#include <stdexcept>

namespace FileOps {

// ── Helpers ──────────────────────────────────────────────────────

static std::string WstrToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), out.data(), len, nullptr, nullptr);
    return out;
}

// Generate a unique filename if target already exists
static fs::path MakeUnique(const fs::path& target)
{
    if (!fs::exists(target))
        return target;

    fs::path parent  = target.parent_path();
    fs::path stem    = target.stem();
    fs::path ext     = target.extension();

    for (int i = 1; i < 10000; ++i)
    {
        std::string newName = stem.string() + "_(" + std::to_string(i) + ")" + ext.string();
        fs::path candidate = parent / newName;
        if (!fs::exists(candidate))
            return candidate;
    }
    return target; // fallback (will likely fail)
}

// ── ChangeExtension ──────────────────────────────────────────────

BatchResult ChangeExtension(
    const std::vector<fs::path>& files,
    const std::string&           newExtension)
{
    BatchResult batch;
    batch.totalFiles = (int)files.size();

    for (const auto& src : files)
    {
        OpResult r;
        r.sourcePath = src;

        try
        {
            if (!fs::exists(src))
            {
                r.success  = false;
                r.errorMsg = "File not found: " + src.filename().string();
                batch.results.push_back(r);
                ++batch.failCount;
                continue;
            }

            // Build new path: parent / stem + newExtension
            fs::path parent = src.parent_path();
            fs::path stem   = src.stem();

            std::string newStem = stem.string();
            // If stem is empty (e.g. ".gitignore"), keep as-is
            if (newStem.empty())
                newStem = src.filename().string();

            fs::path dest = parent / (newStem + newExtension);

            // Use non-throwing equivalent check
            std::error_code ecEq;
            if (fs::equivalent(src, dest, ecEq) && !ecEq)
            {
                r.success  = true;
                r.destPath = src;
                batch.results.push_back(r);
                ++batch.successCount;
                continue;
            }

            // Resolve conflict
            dest = MakeUnique(dest);

            std::error_code ec;
            fs::rename(src, dest, ec);
            if (ec)
            {
                r.success  = false;
                r.errorMsg = ec.message();
                ++batch.failCount;
            }
            else
            {
                r.success  = true;
                r.destPath = dest;
                ++batch.successCount;
            }
        }
        catch (const std::exception& e)
        {
            r.success  = false;
            r.errorMsg = std::string("exception: ") + e.what();
            ++batch.failCount;
        }
        catch (...)
        {
            r.success  = false;
            r.errorMsg = "Unknown exception in ChangeExtension";
            ++batch.failCount;
        }
        batch.results.push_back(r);
    }
    return batch;
}

// ── TrimChars ────────────────────────────────────────────────────

BatchResult TrimChars(
    const std::vector<fs::path>& files,
    int                          count,
    bool                         fromFront)
{
    BatchResult batch;
    batch.totalFiles = (int)files.size();

    if (count <= 0)
    {
        // Zero trim is a no-op success
        for (const auto& src : files)
        {
            OpResult r;
            r.sourcePath = src;
            r.success    = true;
            r.destPath   = src;
            batch.results.push_back(r);
        }
        batch.successCount = batch.totalFiles;
        return batch;
    }

    for (const auto& src : files)
    {
        OpResult r;
        r.sourcePath = src;

        if (!fs::exists(src))
        {
            r.success  = false;
            r.errorMsg = "File not found";
            batch.results.push_back(r);
            ++batch.failCount;
            continue;
        }

        fs::path parent  = src.parent_path();
        std::string stem = src.stem().string();
        std::string ext  = src.extension().string();

        if ((int)stem.size() <= count)
        {
            r.success  = false;
            r.errorMsg = "Stem too short (" + std::to_string(stem.size()) +
                         " chars) to trim " + std::to_string(count);
            batch.results.push_back(r);
            ++batch.failCount;
            continue;
        }

        std::string newStem;
        if (fromFront)
            newStem = stem.substr(count);
        else
            newStem = stem.substr(0, stem.size() - count);

        if (newStem.empty())
        {
            r.success  = false;
            r.errorMsg = "Resulting filename would be empty";
            batch.results.push_back(r);
            ++batch.failCount;
            continue;
        }

        fs::path dest = parent / (newStem + ext);
        if (fs::exists(dest) && !fs::equivalent(src, dest))
            dest = MakeUnique(dest);

        std::error_code ec;
        fs::rename(src, dest, ec);
        if (ec)
        {
            r.success  = false;
            r.errorMsg = ec.message();
            ++batch.failCount;
        }
        else
        {
            r.success  = true;
            r.destPath = dest;
            ++batch.successCount;
        }
        batch.results.push_back(r);
    }
    return batch;
}

// ── MoveToFolder ─────────────────────────────────────────────────

BatchResult MoveToFolder(
    const std::vector<fs::path>& files,
    const fs::path&              targetFolder)
{
    BatchResult batch;
    batch.totalFiles = (int)files.size();

    std::error_code ec;
    if (!fs::exists(targetFolder))
    {
        fs::create_directories(targetFolder, ec);
        if (ec)
        {
            // Mark all files as failed
            for (const auto& src : files)
            {
                OpResult r;
                r.sourcePath = src;
                r.success    = false;
                r.errorMsg   = "Cannot create folder: " + ec.message();
                batch.results.push_back(r);
            }
            batch.failCount = batch.totalFiles;
            return batch;
        }
    }

    if (!fs::is_directory(targetFolder))
    {
        for (const auto& src : files)
        {
            OpResult r;
            r.sourcePath = src;
            r.success    = false;
            r.errorMsg   = "Target is not a directory";
            batch.results.push_back(r);
        }
        batch.failCount = batch.totalFiles;
        return batch;
    }

    for (const auto& src : files)
    {
        OpResult r;
        r.sourcePath = src;

        if (!fs::exists(src))
        {
            r.success  = false;
            r.errorMsg = "File not found";
            batch.results.push_back(r);
            ++batch.failCount;
            continue;
        }

        fs::path dest = targetFolder / src.filename();
        dest = MakeUnique(dest);

        std::error_code ec2;
        fs::rename(src, dest, ec2);
        if (ec2)
        {
            r.success  = false;
            r.errorMsg = ec2.message();
            ++batch.failCount;
        }
        else
        {
            r.success  = true;
            r.destPath = dest;
            ++batch.successCount;
        }
        batch.results.push_back(r);
    }
    return batch;
}

} // namespace FileOps
