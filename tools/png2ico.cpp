// png2ico — Minimal PNG to ICO converter for Windows Resource Compiler
// Usage: png2ico <input.png> <output.ico> [size]
// Creates a proper Windows 3.00 format ICO with PNG-encoded image data
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <Windows.h>

#pragma pack(push, 1)
struct IcoHeader {
    uint16_t reserved = 0;
    uint16_t type     = 1; // ICO
    uint16_t count    = 1;
};

struct IcoDirEntry {
    uint8_t  width;
    uint8_t  height;
    uint8_t  colors;
    uint8_t  reserved;
    uint16_t planes;
    uint16_t bpp;
    uint32_t size;
    uint32_t offset;
};
#pragma pack(pop)

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 3)
    {
        wprintf(L"Usage: png2ico <input.png> <output.ico> [size]\n");
        return 1;
    }

    const wchar_t* inPath  = argv[1];
    const wchar_t* outPath = argv[2];
    int iconSize = (argc >= 4) ? _wtoi(argv[3]) : 256;

    // Read PNG file
    HANDLE hIn = CreateFileW(inPath, GENERIC_READ, FILE_SHARE_READ,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hIn == INVALID_HANDLE_VALUE) { wprintf(L"Cannot open: %s\n", inPath); return 1; }

    DWORD pngSize = GetFileSize(hIn, nullptr);
    uint8_t* pngData = (uint8_t*)malloc(pngSize);
    DWORD bytesRead;
    ReadFile(hIn, pngData, pngSize, &bytesRead, nullptr);
    CloseHandle(hIn);

    // Clamp icon size to 0-255 (ICO spec uses 1 byte; 0 means 256)
    if (iconSize > 256) iconSize = 256;
    uint8_t szByte = (iconSize == 256) ? 0 : (uint8_t)iconSize;

    // Build ICO file
    IcoHeader hdr;
    hdr.count = 1;

    IcoDirEntry entry;
    entry.width    = szByte;
    entry.height   = szByte;
    entry.colors   = 0;
    entry.reserved = 0;
    entry.planes   = 1;
    entry.bpp      = 32;
    entry.size     = pngSize;
    entry.offset   = sizeof(IcoHeader) + sizeof(IcoDirEntry);

    // Write ICO
    HANDLE hOut = CreateFileW(outPath, GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hOut == INVALID_HANDLE_VALUE) { wprintf(L"Cannot create: %s\n", outPath); free(pngData); return 1; }

    DWORD written;
    WriteFile(hOut, &hdr,   sizeof(hdr),   &written, nullptr);
    WriteFile(hOut, &entry, sizeof(entry), &written, nullptr);
    WriteFile(hOut, pngData, pngSize,      &written, nullptr);
    CloseHandle(hOut);

    free(pngData);
    wprintf(L"Created ICO: %s (%u bytes)\n", outPath, pngSize + sizeof(hdr) + sizeof(entry));
    return 0;
}
