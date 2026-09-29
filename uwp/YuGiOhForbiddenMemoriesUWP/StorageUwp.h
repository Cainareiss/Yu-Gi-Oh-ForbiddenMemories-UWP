#pragma once

#include <string>
#include <vector>
#include <stdio.h>

struct RomInfo
{
    std::wstring path;
    std::wstring fileName;
    std::wstring serial;
    uint64_t fileSize = 0;
    bool isValid = false;
};

class StorageUwp
{
public:
    static void Initialize();
    static std::wstring GetLocalFolderPath();
    static std::wstring GetSavesFolderPath();
    static std::wstring GetModsFolderPath();
    static std::wstring GetActiveDiscPath();
    static void SetActiveDiscPath(const std::wstring& path);

    static void SetActiveDiscFile(Windows::Storage::StorageFile^ file);
    static Windows::Storage::StorageFile^ GetActiveDiscFile();

    static FILE* OpenFile(const std::wstring& filePath, const wchar_t* mode);
    static int OpenFileDescriptor(const std::wstring& filePath, int flags, int mode);
    static int CheckAccess(const std::wstring& filePath, int mode);

    static std::vector<RomInfo> ScanAvailableRoms();
    static RomInfo VerifyDisc(const std::wstring& filePath);
};
