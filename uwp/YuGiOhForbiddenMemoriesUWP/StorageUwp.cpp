#define _CRT_SECURE_NO_WARNINGS
#include "StorageUwp.h"
#include <windows.h>
#include <wrl.h>
#include <ppltasks.h>
#include <io.h>
#include <fcntl.h>
#include <fstream>
#include <algorithm>

enum HANDLE_ACCESS_OPTIONS
{
    HAO_NONE            = 0,
    HAO_READ_ATTRIBUTES = 0x80,
    HAO_READ            = 0x120089,
    HAO_WRITE           = 0x120116,
    HAO_DELETE          = 0x10000
};

enum HANDLE_SHARING_OPTIONS
{
    HSO_SHARE_NONE      = 0,
    HSO_SHARE_READ      = 0x1,
    HSO_SHARE_WRITE     = 0x2,
    HSO_SHARE_DELETE    = 0x4
};

enum HANDLE_OPTIONS
{
    HO_NONE             = 0
};

MIDL_INTERFACE("5CA296B2-2C25-4D22-B785-B885C8201E6A")
IStorageItemHandleAccess : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE Create(
        HANDLE_ACCESS_OPTIONS accessOptions,
        HANDLE_SHARING_OPTIONS sharingOptions,
        HANDLE_OPTIONS options,
        void* oplockBreakingHandler,
        HANDLE* interopHandle) = 0;
};

using namespace Windows::Storage;

static std::wstring s_localFolder;
static std::wstring s_activeDiscPath;
static Windows::Storage::StorageFile^ s_activeStorageFile = nullptr;

static void LogStorage(const std::wstring& msg)
{
    OutputDebugStringW((L"[UWP_STORAGE] " + msg + L"\n").c_str());
    try
    {
        auto folder = ApplicationData::Current->LocalFolder;
        if (folder != nullptr)
        {
            std::wstring logPath = folder->Path->Data() + std::wstring(L"\\boot_log.txt");
            FILE* f = _wfopen(logPath.c_str(), L"a, ccs=UTF-8");
            if (f)
            {
                fwprintf(f, L"[STORAGE] %s\n", msg.c_str());
                fflush(f);
                fclose(f);
            }
        }
    }
    catch (...) {}
}

void StorageUwp::Initialize()
{
    auto folder = ApplicationData::Current->LocalFolder;
    s_localFolder = folder->Path->Data();

    // Create subfolders if needed
    CreateDirectoryW((s_localFolder + L"\\saves").c_str(), nullptr);
    CreateDirectoryW((s_localFolder + L"\\game").c_str(), nullptr);
    CreateDirectoryW((s_localFolder + L"\\mods").c_str(), nullptr);
    CreateDirectoryW((s_localFolder + L"\\logs").c_str(), nullptr);

    // Read cached disc-path.txt (UTF-8 binary mode)
    std::wstring pathFile = s_localFolder + L"\\disc-path.txt";
    std::ifstream stream(pathFile, std::ios::binary);
    if (stream.is_open())
    {
        std::string utf8Path;
        std::getline(stream, utf8Path);
        while (!utf8Path.empty() && (utf8Path.back() == '\r' || utf8Path.back() == '\n' || utf8Path.back() == ' ' || utf8Path.back() == '\0'))
        {
            utf8Path.pop_back();
        }
        // Strip UTF-8 BOM if present
        if (utf8Path.size() >= 3 && (unsigned char)utf8Path[0] == 0xEF && (unsigned char)utf8Path[1] == 0xBB && (unsigned char)utf8Path[2] == 0xBF)
        {
            utf8Path = utf8Path.substr(3);
        }
        if (!utf8Path.empty())
        {
            int len = MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, nullptr, 0);
            if (len > 0)
            {
                std::wstring widePath(len - 1, 0);
                MultiByteToWideChar(CP_UTF8, 0, utf8Path.c_str(), -1, &widePath[0], len);
                s_activeDiscPath = widePath;
            }
        }
    }
}

std::wstring StorageUwp::GetLocalFolderPath()
{
    if (s_localFolder.empty()) Initialize();
    return s_localFolder;
}

std::wstring StorageUwp::GetSavesFolderPath()
{
    return GetLocalFolderPath() + L"\\saves";
}

std::wstring StorageUwp::GetModsFolderPath()
{
    return GetLocalFolderPath() + L"\\mods";
}

std::wstring StorageUwp::GetActiveDiscPath()
{
    return s_activeDiscPath;
}

void StorageUwp::SetActiveDiscPath(const std::wstring& path)
{
    s_activeDiscPath = path;
    std::wstring pathFile = GetLocalFolderPath() + L"\\disc-path.txt";
    std::ofstream stream(pathFile, std::ios::binary | std::ios::trunc);
    if (stream.is_open())
    {
        int len = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (len > 0)
        {
            std::string utf8(len - 1, 0);
            WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &utf8[0], len, nullptr, nullptr);
            stream << utf8;
        }
    }
}

void StorageUwp::SetActiveDiscFile(Windows::Storage::StorageFile^ file)
{
    s_activeStorageFile = file;
    if (file != nullptr)
    {
        SetActiveDiscPath(file->Path->Data());
        LogStorage(L"Active disc file set to: " + std::wstring(file->Path->Data()));
    }
}

Windows::Storage::StorageFile^ StorageUwp::GetActiveDiscFile()
{
    return s_activeStorageFile;
}

static Windows::Storage::StorageFile^ ResolveStorageFile(const std::wstring& filePath)
{
    if (s_activeStorageFile != nullptr && s_activeStorageFile->Path->Data() == filePath)
    {
        return s_activeStorageFile;
    }

    HANDLE ev = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS);
    if (!ev) return nullptr;

    Windows::Storage::StorageFile^ result = nullptr;
    try
    {
        auto asyncOp = Windows::Storage::StorageFile::GetFileFromPathAsync(ref new Platform::String(filePath.c_str()));
        auto task = concurrency::create_task(asyncOp);
        task.then([&result, ev](concurrency::task<Windows::Storage::StorageFile^> t)
        {
            try
            {
                result = t.get();
            }
            catch (...)
            {
                result = nullptr;
            }
            SetEvent(ev);
        });

        // Wait up to 3 seconds for file retrieval
        WaitForSingleObjectEx(ev, 3000, FALSE);
        if (result != nullptr)
        {
            s_activeStorageFile = result;
        }
    }
    catch (...)
    {
        result = nullptr;
    }

    CloseHandle(ev);
    return result;
}

FILE* StorageUwp::OpenFile(const std::wstring& filePath, const wchar_t* mode)
{
    FILE* f = nullptr;
    errno_t err = _wfopen_s(&f, filePath.c_str(), mode);
    if (err == 0 && f != nullptr)
    {
        return f;
    }

    LogStorage(L"Direct _wfopen_s failed (errno=" + std::to_wstring(err) + L"), brokering via WinRT for: " + filePath);

    Windows::Storage::StorageFile^ file = ResolveStorageFile(filePath);
    if (file == nullptr)
    {
        LogStorage(L"ResolveStorageFile failed for: " + filePath);
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IStorageItemHandleAccess> handleAccess;
    HRESULT hr = reinterpret_cast<IUnknown*>(file)->QueryInterface(IID_PPV_ARGS(&handleAccess));
    if (FAILED(hr) || !handleAccess)
    {
        LogStorage(L"IStorageItemHandleAccess QueryInterface failed, hr=" + std::to_wstring(hr));
        return nullptr;
    }

    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE_ACCESS_OPTIONS accessOptions = static_cast<HANDLE_ACCESS_OPTIONS>(HANDLE_ACCESS_OPTIONS::HAO_READ | HANDLE_ACCESS_OPTIONS::HAO_READ_ATTRIBUTES);
    HANDLE_SHARING_OPTIONS shareOptions = HANDLE_SHARING_OPTIONS::HSO_SHARE_READ;
    if (wcschr(mode, L'w') || wcschr(mode, L'+') || wcschr(mode, L'a'))
    {
        accessOptions = static_cast<HANDLE_ACCESS_OPTIONS>(accessOptions | HANDLE_ACCESS_OPTIONS::HAO_WRITE);
        shareOptions = static_cast<HANDLE_SHARING_OPTIONS>(shareOptions | HANDLE_SHARING_OPTIONS::HSO_SHARE_WRITE);
    }

    hr = handleAccess->Create(accessOptions, shareOptions, HANDLE_OPTIONS::HO_NONE, nullptr, &hFile);
    if (FAILED(hr) || hFile == INVALID_HANDLE_VALUE)
    {
        LogStorage(L"IStorageItemHandleAccess::Create failed, hr=" + std::to_wstring(hr));
        return nullptr;
    }

    int flags = _O_RDONLY | _O_BINARY;
    if (wcschr(mode, L'w') || wcschr(mode, L'+') || wcschr(mode, L'a'))
    {
        flags = _O_RDWR | _O_BINARY;
    }

    int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hFile), flags);
    if (fd == -1)
    {
        LogStorage(L"_open_osfhandle failed");
        CloseHandle(hFile);
        return nullptr;
    }

    char charMode[8] = { 0 };
    for (int i = 0; mode[i] && i < 7; i++)
    {
        charMode[i] = static_cast<char>(mode[i]);
    }

    f = _fdopen(fd, charMode);
    if (!f)
    {
        LogStorage(L"_fdopen failed");
        _close(fd);
        return nullptr;
    }

    LogStorage(L"Brokered FILE* successfully created for: " + filePath);
    return f;
}

int StorageUwp::OpenFileDescriptor(const std::wstring& filePath, int flags, int mode)
{
    int fd = _wopen(filePath.c_str(), flags, mode);
    if (fd != -1)
    {
        return fd;
    }

    LogStorage(L"Direct _wopen failed, brokering fd via WinRT for: " + filePath);

    Windows::Storage::StorageFile^ file = ResolveStorageFile(filePath);
    if (file == nullptr)
    {
        return -1;
    }

    Microsoft::WRL::ComPtr<IStorageItemHandleAccess> handleAccess;
    HRESULT hr = reinterpret_cast<IUnknown*>(file)->QueryInterface(IID_PPV_ARGS(&handleAccess));
    if (FAILED(hr) || !handleAccess)
    {
        return -1;
    }

    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE_ACCESS_OPTIONS accessOptions = static_cast<HANDLE_ACCESS_OPTIONS>(HANDLE_ACCESS_OPTIONS::HAO_READ | HANDLE_ACCESS_OPTIONS::HAO_READ_ATTRIBUTES);
    HANDLE_SHARING_OPTIONS shareOptions = HANDLE_SHARING_OPTIONS::HSO_SHARE_READ;
    if (flags & (_O_WRONLY | _O_RDWR))
    {
        accessOptions = static_cast<HANDLE_ACCESS_OPTIONS>(accessOptions | HANDLE_ACCESS_OPTIONS::HAO_WRITE);
        shareOptions = static_cast<HANDLE_SHARING_OPTIONS>(shareOptions | HANDLE_SHARING_OPTIONS::HSO_SHARE_WRITE);
    }

    hr = handleAccess->Create(accessOptions, shareOptions, HANDLE_OPTIONS::HO_NONE, nullptr, &hFile);
    if (FAILED(hr) || hFile == INVALID_HANDLE_VALUE)
    {
        return -1;
    }

    fd = _open_osfhandle(reinterpret_cast<intptr_t>(hFile), flags);
    if (fd == -1)
    {
        CloseHandle(hFile);
        return -1;
    }

    LogStorage(L"Brokered fd=" + std::to_wstring(fd) + L" successfully created for: " + filePath);
    return fd;
}

int StorageUwp::CheckAccess(const std::wstring& filePath, int mode)
{
    if (_waccess(filePath.c_str(), mode) == 0)
    {
        return 0;
    }

    Windows::Storage::StorageFile^ file = ResolveStorageFile(filePath);
    if (file != nullptr)
    {
        return 0;
    }

    return -1;
}

RomInfo StorageUwp::VerifyDisc(const std::wstring& filePath)
{
    RomInfo info;
    info.path = filePath;

    size_t lastSlash = filePath.find_last_of(L"\\/");
    info.fileName = (lastSlash != std::wstring::npos) ? filePath.substr(lastSlash + 1) : filePath;

    FILE* f = StorageUwp::OpenFile(filePath, L"rb");
    if (!f)
    {
        LogStorage(L"VerifyDisc: Failed to open ROM: " + filePath);
        return info;
    }

    _fseeki64(f, 0, SEEK_END);
    info.fileSize = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);

    LogStorage(L"VerifyDisc: ROM opened, size=" + std::to_wstring(info.fileSize) + L" bytes");

    // Minimum raw CD sector size is 2352 bytes. The disc should be > 50 MB.
    if (info.fileSize > 50 * 1024 * 1024)
    {
        // Search in the first 256 sectors (ISO 9660 directory structure)
        const size_t bufferSize = 2352 * 256;
        std::vector<char> buffer(bufferSize);
        size_t read = fread(buffer.data(), 1, bufferSize, f);

        std::string bufferStr(buffer.data(), read);
        if (bufferStr.find("SLUS_014.11") != std::string::npos ||
            bufferStr.find("SLUS-01411") != std::string::npos ||
            bufferStr.find("SLUS_01411") != std::string::npos ||
            bufferStr.find("CD001") != std::string::npos)
        {
            info.isValid = true;
            info.serial = L"SLUS-01411 (USA)";
            LogStorage(L"VerifyDisc: PS1 signature confirmed! isValid=true");
        }
        else
        {
            LogStorage(L"VerifyDisc: PS1 signature NOT found in buffer!");
        }
    }
    else
    {
        LogStorage(L"VerifyDisc: file size too small: " + std::to_wstring(info.fileSize));
    }

    fclose(f);
    return info;
}

std::vector<RomInfo> StorageUwp::ScanAvailableRoms()
{
    std::vector<RomInfo> list;
    std::wstring searchFolders[] = {
        GetLocalFolderPath(),
        GetLocalFolderPath() + L"\\game",
        Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Data(),
        Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Data() + std::wstring(L"\\game")
    };

    for (const auto& folder : searchFolders)
    {
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW((folder + L"\\*.bin").c_str(), &findData);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    std::wstring fullPath = folder + L"\\" + findData.cFileName;
                    auto verified = VerifyDisc(fullPath);
                    if (verified.isValid)
                    {
                        list.push_back(verified);
                    }
                }
            } while (FindNextFileW(hFind, &findData));
            FindClose(hFind);
        }
    }

    return list;
}

extern "C" {
    FILE* UWP_OpenFile(const wchar_t* path, const wchar_t* mode)
    {
        if (!path || !mode) return nullptr;
        return StorageUwp::OpenFile(path, mode);
    }

    int UWP_OpenFD(const wchar_t* path, int flags, int mode)
    {
        if (!path) return -1;
        return StorageUwp::OpenFileDescriptor(path, flags, mode);
    }

    int UWP_Access(const wchar_t* path, int mode)
    {
        if (!path) return -1;
        return StorageUwp::CheckAccess(path, mode);
    }
}
