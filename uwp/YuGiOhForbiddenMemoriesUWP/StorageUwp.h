#pragma once

// Reconstructed frontend contract. The original complete file was not present
// in the scoped session evidence; see recovery/snapshot-provenance.json.
#include <cstdint>
#include <string>
#include <vector>

struct RomInfo
{
    std::wstring path;
    std::wstring name;
    uint64_t fileSize = 0;
    bool isValid = false;
};

class StorageUwp
{
public:
    static void Initialize();
    static std::wstring GetLocalFolderPath();
    static std::wstring GetActiveDiscPath();
    static void SetActiveDiscPath(const std::wstring& path);
    static void SetActiveDiscFile(Windows::Storage::StorageFile^ file);
    static std::vector<RomInfo> ScanAvailableRoms();
    static RomInfo VerifyDisc(const std::wstring& path);

    // Returns a fresh native HANDLE as intptr_t; the caller must close it.
    static intptr_t OpenReadHandle(const std::wstring& path);
};

// Transfer only the OS handle across the host/core CRT boundary. FILE* and CRT
// file descriptors belong to the runtime which created them.
extern "C" intptr_t UWP_OpenReadHandle(const char* pathUtf8);


