#include "ArchiveOps.h"
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <spdlog/spdlog.h>
#include <cstring>
#include <sstream>
#include <fstream>
#include <algorithm>

namespace ArchiveOps {

// ── Helpers ──────────────────────────────────────────────────────

static std::string WstrToUtf8(const std::wstring& ws)
{
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring Utf8ToWstr(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), ws.data(), len);
    return ws;
}

static fs::path GetExeDir()
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0) return fs::path();
    return fs::path(buf).parent_path();
}

static bool DirExists(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

// ── Run 7za.exe and capture output ───────────────────────────────

static bool Run7za(const std::wstring& args, std::string* pStdout = nullptr)
{
    fs::path sevenZip = GetExeDir() / "7za.exe";

    // Also try tools/
    if (!fs::exists(sevenZip))
        sevenZip = GetExeDir().parent_path().parent_path() / "tools" / "7za.exe";

    if (!fs::exists(sevenZip))
    {
        spdlog::error("ArchiveOps: 7za.exe not found at {}", sevenZip.string());
        return false;
    }

    std::wstring cmdLine = L"\"" + sevenZip.wstring() + L"\" " + args;

    // Create pipes for stdout
    HANDLE hRead = nullptr, hWrite = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;

    spdlog::debug("ArchiveOps: running 7za: {}", WstrToUtf8(cmdLine));

    if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr,
                        GetExeDir().wstring().c_str(), &si, &pi))
    {
        spdlog::error("ArchiveOps: CreateProcess failed");
        CloseHandle(hWrite);
        CloseHandle(hRead);
        return false;
    }

    // Close parent's write end IMMEDIATELY so the pipe signals EOF
    // when the child exits.  Otherwise child blocks on WriteFile when
    // the pipe buffer (4 KB) fills up, deadlocking with WaitForSingleObject.
    CloseHandle(hWrite);

    // Read stdout/stderr — drain pipe so child never blocks on write
    if (pStdout)
    {
        pStdout->clear();
        char buf[4096];
        DWORD read = 0;
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &read, nullptr) && read > 0)
        {
            buf[read] = '\0';
            *pStdout += buf;
        }
    }

    // Now wait for process to finish (child already exited or will exit soon)
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(hRead);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0)
    {
        std::string out = pStdout ? *pStdout : "(none)";
        spdlog::error("ArchiveOps: 7za exit={} output={}", exitCode, out);
    }
    else
    {
        spdlog::debug("ArchiveOps: 7za OK");
    }
    return exitCode == 0;
}

// ── 7z Extract ───────────────────────────────────────────────────

BatchArchiveResult Extract7z(const fs::path& archivePath,
                              const fs::path& outDir,
                              ProgressCb progress)
{
    BatchArchiveResult batch;
    batch.totalFiles = 1;

    fs::path absPath = fs::absolute(archivePath);
    fs::path absOut  = fs::absolute(outDir);

    // Create output directory
    std::error_code ec;
    if (!fs::exists(absOut))
        fs::create_directories(absOut, ec);

    ArchiveResult r;
    r.sourcePath = absPath.string();

    // 7za x <archive> -o<outDir> -y  (x = extract with full paths)
    std::wstring args = L"x -scsUTF-8 \"" + absPath.wstring() + L"\" -o\"" + absOut.wstring() + L"\" -y";
    std::string _out; bool ok = Run7za(args, &_out);

    r.success  = ok;
    r.destPath = absOut.string();
    if (!ok)
        r.errorMsg = "7z extraction failed";

    batch.results.push_back(r);
    batch.okFiles   = ok ? 1 : 0;
    batch.failFiles = ok ? 0 : 1;

    if (progress) progress(1, 1, ok ? "7z 解压完成" : "7z 解压失败");
    return batch;
}

// ── 7z Compress ──────────────────────────────────────────────────

BatchArchiveResult Compress7z(const std::vector<fs::path>& paths,
                               const fs::path& archivePath,
                               ProgressCb progress)
{
    BatchArchiveResult batch;
    batch.totalFiles = (int)paths.size();

    fs::path absArchive = fs::absolute(archivePath);

    // Build a temporary list file for 7za
    // (avoids command-line length limit)
    fs::path listFile = fs::temp_directory_path() / "filetools_7zlist.txt";
    {
        std::ofstream f(listFile, std::ios::binary);
        if (!f.is_open())
        {
            batch.failFiles = batch.totalFiles;
            for (const auto& p : paths)
            {
                ArchiveResult r;
                r.sourcePath = p.string();
                r.success = false;
                r.errorMsg = "Cannot create list file";
                batch.results.push_back(r);
            }
            return batch;
        }
        // UTF-8 BOM — 7za needs it for non-ASCII paths
        f.write("\xEF\xBB\xBF", 3);
        for (const auto& p : paths)
        {
            std::string absPath = fs::absolute(p).u8string();
            f.write(absPath.c_str(), absPath.size());
            f.write("\n", 1);
            spdlog::debug("ArchiveOps: list: {}", absPath);
        }
    }
    spdlog::debug("ArchiveOps: list file written at {}", listFile.string());

    // 7za a -tzip <archive> @<listfile> -y
    std::wstring args = L"a -t7z -scsUTF-8 \"" + absArchive.wstring() + L"\" @" +
                        listFile.wstring() + L" -y";

    if (progress) progress(0, 1, "正在压缩 7z...");
    bool ok = Run7za(args);

    // Clean up list file
    std::error_code ec;
    fs::remove(listFile, ec);

    for (const auto& p : paths)
    {
        ArchiveResult r;
        r.sourcePath = p.string();
        r.success    = ok;
        r.destPath   = absArchive.string();
        if (!ok) r.errorMsg = "7z compression failed";
        batch.results.push_back(r);
    }

    batch.okFiles   = ok ? batch.totalFiles : 0;
    batch.failFiles = ok ? 0 : batch.totalFiles;

    if (progress) progress(1, 1, ok ? "7z 压缩完成" : "7z 压缩失败");
    return batch;
}

// ── Zip extract via Windows COM Shell ─────────────────────────────
// Uses Shell.Application which handles ZIP natively on Windows.
// This works on zip files that 7z might reject.

BatchArchiveResult ExtractZip(const fs::path& archivePath,
                               const fs::path& outDir,
                               ProgressCb progress)
{
    BatchArchiveResult batch;
    batch.totalFiles = 1;

    ArchiveResult r;
    r.sourcePath = archivePath.string();

    fs::path absOut = fs::absolute(outDir);

    // Create output dir
    std::error_code ec;
    if (!fs::exists(absOut))
        fs::create_directories(absOut, ec);

    // COM initialisation (caller should have done CoInitialize)
    IShellDispatch* pShell = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IShellDispatch, (void**)&pShell);
    if (FAILED(hr) || !pShell)
    {
        r.success  = false;
        r.errorMsg = "COM Shell init failed";
        batch.results.push_back(r);
        batch.failFiles = 1;
        return batch;
    }

    // Get source folder (the zip file as a folder)
    Folder* pSrcFolder = nullptr;
    {
        VARIANT vSrc;
        VariantInit(&vSrc);
        vSrc.vt = VT_BSTR;
        std::wstring wPath = archivePath.wstring();
        vSrc.bstrVal = SysAllocString(wPath.c_str());
        hr = pShell->NameSpace(vSrc, &pSrcFolder);
        VariantClear(&vSrc);
    }

    if (FAILED(hr) || !pSrcFolder)
    {
        pShell->Release();
        // Try 7za as fallback
        spdlog::debug("ArchiveOps: Shell namespace failed for zip, trying 7za -tzip");
        return Extract7z(archivePath, outDir, progress);
    }

    // Get destination folder
    Folder* pDstFolder = nullptr;
    {
        VARIANT vDst;
        VariantInit(&vDst);
        vDst.vt = VT_BSTR;
        std::wstring wOut = absOut.wstring();
        vDst.bstrVal = SysAllocString(wOut.c_str());
        hr = pShell->NameSpace(vDst, &pDstFolder);
        VariantClear(&vDst);
    }

    if (FAILED(hr) || !pDstFolder)
    {
        pSrcFolder->Release();
        pShell->Release();
        r.success  = false;
        r.errorMsg = "Destination folder not accessible";
        batch.results.push_back(r);
        batch.failFiles = 1;
        return batch;
    }

    // Copy all items from source to dest
    FolderItems* pItems = nullptr;
    hr = pSrcFolder->Items(&pItems);
    if (SUCCEEDED(hr) && pItems)
    {
        long count = 0;
        pItems->get_Count(&count);

        if (count > 0)
        {
            VARIANT vOpt;
            VariantInit(&vOpt);
            vOpt.vt = VT_I4;
            vOpt.lVal = FOF_NO_UI; // silent — no progress dialog

            if (progress)
                progress(0, count, "正在解压 zip...");

            // CopyHere expects VARIANT for source — wrap FolderItems as VT_DISPATCH
            VARIANT vSrc;
            VariantInit(&vSrc);
            vSrc.vt = VT_DISPATCH;
            pItems->QueryInterface(IID_IDispatch, (void**)&vSrc.pdispVal);
            pDstFolder->CopyHere(vSrc, vOpt);
            if (vSrc.pdispVal) vSrc.pdispVal->Release();
            VariantClear(&vSrc);
            VariantClear(&vOpt);
        }

        pItems->Release();
    }

    pDstFolder->Release();
    pSrcFolder->Release();
    pShell->Release();

    r.success  = true;
    r.destPath = absOut.string();
    batch.results.push_back(r);
    batch.okFiles = 1;

    if (progress) progress(1, 1, "zip 解压完成");
    return batch;
}

// ── Auto-detect extraction ────────────────────────────────────────

BatchArchiveResult ExtractAuto(const fs::path& archivePath,
                                const fs::path& outDir,
                                ProgressCb progress)
{
    // Check magic bytes to determine format
    std::ifstream f(archivePath, std::ios::binary);
    if (!f.is_open())
    {
        BatchArchiveResult batch;
        batch.totalFiles = 1;
        batch.failFiles  = 1;
        ArchiveResult r;
        r.sourcePath = archivePath.string();
        r.success    = false;
        r.errorMsg   = "Cannot open file";
        batch.results.push_back(r);
        return batch;
    }

    unsigned char magic[6] = {};
    f.read((char*)magic, sizeof(magic));
    f.close();

    // 7z magic: 37 7A BC AF 27 1C
    bool is7z = (magic[0] == 0x37 && magic[1] == 0x7A && magic[2] == 0xBC &&
                 magic[3] == 0xAF && magic[4] == 0x27 && magic[5] == 0x1C);

    // Zip magic: 50 4B 03 04  (PK..)
    bool isZip = (magic[0] == 0x50 && magic[1] == 0x4B &&
                  magic[2] == 0x03 && magic[3] == 0x04);

    spdlog::debug("ArchiveOps: magic={:02X}{:02X}{:02X}{:02X} is7z={} isZip={}",
                  magic[0], magic[1], magic[2], magic[3], is7z, isZip);

    if (is7z)
        return Extract7z(archivePath, outDir, progress);

    if (isZip)
    {
        // Try Windows COM first (handles weird zip variants), fallback to 7za -tzip
        auto result = ExtractZip(archivePath, outDir, progress);
        if (result.AllOk()) return result;
        spdlog::debug("ArchiveOps: Shell zip failed, trying 7za -tzip...");
        return Extract7z(archivePath, outDir, progress); // 7za handles zip too
    }

    // Unknown — try 7z first, then zip
    spdlog::debug("ArchiveOps: unknown format, trying 7z then zip...");
    auto result = Extract7z(archivePath, outDir, progress);
    if (result.AllOk()) return result;
    return ExtractZip(archivePath, outDir, progress);
}

} // namespace ArchiveOps
