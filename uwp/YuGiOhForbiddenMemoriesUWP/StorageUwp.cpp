// Reconstructed storage implementation, not an exact historical source copy.
// All WinRT asynchronous operations use continuations; never wait on the UI
// thread for a broker operation that may require that same thread to complete.
#include "pch.h"
#include "StorageUwp.h"
#include <windows.h>
#include <fileapifromapp.h>
#include <wrl/client.h>
#include <agile.h>
#include <ppltasks.h>
#include <algorithm>
#include <cerrno>
#include <mutex>

using namespace Windows::Storage;
using namespace Windows::Storage::AccessCache;
using Microsoft::WRL::ComPtr;

namespace
{
    // IStorageItemHandleAccess's published ABI (windowsstoragecom.h). The SDK
    // 26100 header also pulls shell proxies with desktop-only argument types
    // into an APP-family build. Declare only this COM interface so Xbox builds
    // do not depend on the desktop shell headers or DLL imports.
    MIDL_INTERFACE("5CA296B2-2C25-4D22-B785-B885C8201E6A")
    BrokeredFileHandleAccess : public IUnknown
    {
        virtual HRESULT STDMETHODCALLTYPE Create(DWORD accessOptions,
            DWORD sharingOptions, DWORD options, IUnknown* oplockHandler,
            HANDLE* handle) = 0;
    };

    std::mutex stateMutex;
    std::wstring localPath;
    std::wstring activePath;
    Platform::Agile<StorageFile> activeFile;
    std::once_flag initialized;

    void StorePath(const std::wstring& path)
    {
        try
        {
            ApplicationData::Current->LocalSettings->Values->Insert(
                L"ActiveDiscPath", Windows::Foundation::PropertyValue::CreateString(
                    ref new Platform::String(path.c_str())));
        }
        catch (Platform::Exception^) {}
    }

    std::wstring ReadStoredPath()
    {
        auto values = ApplicationData::Current->LocalSettings->Values;
        for (auto key : {L"ActiveDiscPath", L"SelectedRomPath", L"RomPath", L"LastRomPath"})
        {
            if (!values->HasKey(ref new Platform::String(key))) continue;
            auto value = dynamic_cast<Platform::String^>(values->Lookup(ref new Platform::String(key)));
            if (value && !value->IsEmpty()) return value->Data();
        }
        return {};
    }

    bool HasIsoSignature(const std::vector<unsigned char>& bytes, DWORD count)
    {
        const char* signatures[] = {"CD001", "PLAYSTATION"};
        for (const char* signature : signatures)
        {
            const size_t length = strlen(signature);
            if (std::search(bytes.begin(), bytes.begin() + count,
                            signature, signature + length) != bytes.begin() + count)
                return true;
        }
        return false;
    }
}

void StorageUwp::Initialize()
{
    std::call_once(initialized, []()
    {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            localPath = ApplicationData::Current->LocalFolder->Path->Data();
            activePath = ReadStoredPath();
        }
        // MainPage owns the asynchronous FutureAccessList restore and its
        // generation check. A second restore here could overwrite a newer
        // picker selection after that selection has already reached the UI.
    });
}

std::wstring StorageUwp::GetLocalFolderPath()
{
    Initialize();
    std::lock_guard<std::mutex> lock(stateMutex);
    return localPath;
}

std::wstring StorageUwp::GetActiveDiscPath()
{
    Initialize();
    std::lock_guard<std::mutex> lock(stateMutex);
    return activePath;
}

void StorageUwp::SetActiveDiscPath(const std::wstring& path)
{
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (_wcsicmp(activePath.c_str(), path.c_str()) != 0)
            activeFile = Platform::Agile<StorageFile>();
        activePath = path;
    }
    StorePath(path);
}

void StorageUwp::SetActiveDiscFile(StorageFile^ file)
{
    if (!file) return;
    const std::wstring path = file->Path->Data();
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        activeFile = Platform::Agile<StorageFile>(file);
        activePath = path;
    }
    StorePath(path);
    try { StorageApplicationPermissions::FutureAccessList->AddOrReplace(L"SelectedRom", file); }
    catch (Platform::Exception^) {}
}

intptr_t StorageUwp::OpenReadHandle(const std::wstring& path)
{
    if (path.empty()) { SetLastError(ERROR_INVALID_NAME); return -1; }
    HANDLE handle = CreateFileFromAppW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) return reinterpret_cast<intptr_t>(handle);

    StorageFile^ selected = nullptr;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (_wcsicmp(path.c_str(), activePath.c_str()) == 0)
            selected = activeFile.Get();
    }
    if (selected)
    {
        ComPtr<BrokeredFileHandleAccess> access;
        if (SUCCEEDED(reinterpret_cast<IUnknown*>(selected)->QueryInterface(IID_PPV_ARGS(&access))))
        {
            handle = INVALID_HANDLE_VALUE;
            const HRESULT result = access->Create(0x00120089u, // HAO_READ
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                0, nullptr, &handle); // HO_NONE, synchronous handle
            if (SUCCEEDED(result) && handle != INVALID_HANDLE_VALUE)
                return reinterpret_cast<intptr_t>(handle);
        }
    }
    errno = EACCES;
    return -1;
}

RomInfo StorageUwp::VerifyDisc(const std::wstring& path)
{
    RomInfo info;
    info.path = path;
    const auto slash = path.find_last_of(L"\\/");
    info.name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const intptr_t raw = OpenReadHandle(path);
    if (raw == -1) return info;
    HANDLE handle = reinterpret_cast<HANDLE>(raw);
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(handle, &size) && size.QuadPart >= 1024 * 1024)
    {
        info.fileSize = static_cast<uint64_t>(size.QuadPart);
        std::vector<unsigned char> bytes(65536);
        DWORD count = 0;
        if (ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr))
            info.isValid = HasIsoSignature(bytes, count);
    }
    CloseHandle(handle);
    return info;
}

std::vector<RomInfo> StorageUwp::ScanAvailableRoms()
{
    std::vector<RomInfo> result;
    const auto stored = GetActiveDiscPath();
    if (!stored.empty())
    {
        auto info = VerifyDisc(stored);
        if (info.isValid) result.push_back(info);
    }
    const auto local = GetLocalFolderPath();
    const std::wstring package = Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Data();
    for (const auto& directory : {local, local + L"\\roms", package + L"\\roms", package + L"\\Assets"})
    {
        WIN32_FIND_DATAW item = {};
        HANDLE find = FindFirstFileExFromAppW((directory + L"\\*").c_str(),
            FindExInfoBasic, &item, FindExSearchNameMatch, nullptr, 0);
        if (find == INVALID_HANDLE_VALUE) continue;
        do
        {
            if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const wchar_t* extension = wcsrchr(item.cFileName, L'.');
            if (!extension || (_wcsicmp(extension, L".bin") &&
                _wcsicmp(extension, L".iso") && _wcsicmp(extension, L".img"))) continue;
            const std::wstring path = directory + L"\\" + item.cFileName;
            if (std::any_of(result.begin(), result.end(), [&](const RomInfo& info)
                { return _wcsicmp(info.path.c_str(), path.c_str()) == 0; })) continue;
            auto info = VerifyDisc(path);
            if (info.isValid) result.push_back(info);
        } while (FindNextFileW(find, &item));
        FindClose(find);
    }
    return result;
}

extern "C" intptr_t UWP_OpenReadHandle(const char* pathUtf8)
{
    if (!pathUtf8) { errno = EINVAL; return -1; }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pathUtf8, -1, nullptr, 0);
    if (length <= 0) { errno = EILSEQ; return -1; }
    std::wstring path(static_cast<size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pathUtf8, -1, &path[0], length))
    { errno = EILSEQ; return -1; }
    path.resize(static_cast<size_t>(length - 1));
    return StorageUwp::OpenReadHandle(path);
}


