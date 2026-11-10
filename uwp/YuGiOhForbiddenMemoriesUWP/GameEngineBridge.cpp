#include "GameEngineBridge.h"
#include "GamepadUwp.h"
#include "StorageUwp.h"
#include "pc/platform/uwp_bridge.h"
#include <windows.h>
#include <fstream>
#include <string>
#include <vector>
#include <roapi.h>

static std::string WideToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

extern "C" void UWP_LogMessage(const char* msg)
{
    if (!msg) return;
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
    try
    {
        auto folder = Windows::Storage::ApplicationData::Current->LocalFolder;
        if (folder != nullptr)
        {
            std::wstring logPath = folder->Path->Data() + std::wstring(L"\\boot_log.txt");
            FILE* f = nullptr;
            if (_wfopen_s(&f, logPath.c_str(), L"ab") == 0 && f)
            {
                fwrite(msg, 1, strlen(msg), f);
                fwrite("\n", 1, 1, f);
                fflush(f);
                fclose(f);
            }
        }
    }
    catch (...) {}
}

extern "C" void UWP_SetLastError(const char* err)
{
    if (err)
    {
        GameEngineBridge::Instance().SetLastError(err);
    }
}

GameEngineBridge::GameEngineBridge()
{
    m_audio = std::make_unique<AudioXAudio2>();
}

GameEngineBridge::~GameEngineBridge()
{
    StopGame();
}

GameEngineBridge& GameEngineBridge::Instance()
{
    static GameEngineBridge instance;
    return instance;
}

bool GameEngineBridge::StartGame(D3D12Renderer* renderer, const std::wstring& romPath, const GameSettings& settings)
{
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    if (m_running)
    {
        UWP_LogMessage("[BRIDGE] Duplicate start ignored; existing game kept running");
        return true;
    }
    // A finished std::thread remains joinable; assigning another terminates
    // the entire app, even though m_running has already become false.
    StopGameLocked();

    m_renderer = renderer;
    m_romPath = romPath;
    SetSettings(settings);
    m_firstFramePresented = false;
    SetLastError("");
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_frameReady = false;
        m_pendingFrame.clear();
    }

    std::wstring userDir = StorageUwp::GetLocalFolderPath();

    UWP_LogMessage(("[BRIDGE] StartGame called with ROM: " + WideToUtf8(romPath)).c_str());
    UWP_LogMessage(("[BRIDGE] Local userDir: " + WideToUtf8(userDir)).c_str());

    // Configure the core's own CRT environment in UTF-8.
    auto setEnvironment = [](const char* name, const std::wstring& value) {
        const auto utf8 = WideToUtf8(value);
        return Memories_UWP_SetEnvironment(name, utf8.c_str()) == 0;
    };
    const std::wstring programDir = Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Data();
    if (!setEnvironment("MEMORIES_DISC", romPath) ||
        !setEnvironment("MEMORIES_USER_DIR", userDir) ||
        !setEnvironment("MEMORIES_PROGRAM_DIR", programDir))
    {
        SetLastError("N?o foi poss?vel preparar as pastas do jogo.");
        return false;
    }
    setEnvironment("MEMORIES_CLOCK", L"cooperative");
    setEnvironment("MEMORIES_HEADLESS", L"0");
    setEnvironment("MEMORIES_BACKEND", L"uwp");
    setEnvironment("MEMORIES_INTERNAL_SCALE", std::to_wstring(settings.resolutionScale));
    setEnvironment("MEMORIES_ASPECT", settings.widescreen ? L"2" : L"0");
    setEnvironment("MEMORIES_FILTER", settings.bilinearFilter ? L"1" : L"0");
    setEnvironment("MEMORIES_HD_TEXT", settings.hdText ? L"1" : L"0");
    setEnvironment("MEMORIES_MASTER_VOLUME", std::to_wstring(static_cast<int>(settings.masterVolume * 100.0f + 0.5f)));
    setEnvironment("MEMORIES_MOD_3D_MONSTERS", settings.monsters3D ? L"1" : L"0");
    setEnvironment("MEMORIES_MOD_AI_HARD_MODE", settings.aiHardMode ? L"1" : L"0");
    setEnvironment("MEMORIES_MOD_DROP_MISSING_CARDS", settings.dropMissingCards ? L"1" : L"0");

    m_running = true;
    m_paused = false;
    m_resetRequested = false;

    try
    {
        m_gameThread = std::thread(&GameEngineBridge::GameThreadEntryPoint, this);
    }
    catch (const std::exception& ex)
    {
        m_running = false;
        SetLastError(ex.what());
        UWP_LogMessage("[BRIDGE] Failed to create game worker");
        return false;
    }
    return true;
}

void GameEngineBridge::StopGame()
{
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    StopGameLocked();
}

void GameEngineBridge::StopGameLocked()
{
    m_running = false;
    if (m_gameThread.joinable())
    {
        m_gameThread.join();
    }

    if (m_audio)
    {
        m_audio->Shutdown();
    }
}

void GameEngineBridge::PauseGame()
{
    m_paused = true;
}

void GameEngineBridge::ResumeGame()
{
    m_paused = false;
}

void GameEngineBridge::RequestReset()
{
    m_resetRequested = true;
}

void GameEngineBridge::SetSettings(const GameSettings& settings)
{
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_settings = settings;
    }

    // Settings dialogs run on the XAML/UI thread while the game owns all
    // guest state on its worker thread. Queue changes for Platform_Frame so
    // the native core applies them at a safe VBlank boundary.
    if (m_running)
    {
        Memories_UWP_RequestSetting("internal_scale", settings.resolutionScale);
        Memories_UWP_RequestSetting("aspect", settings.widescreen ? 2 : 0);
        Memories_UWP_RequestSetting("filter", settings.bilinearFilter ? 1 : 0);
        Memories_UWP_RequestSetting("hd_text", settings.hdText ? 1 : 0);
        Memories_UWP_RequestSetting("master_volume",
            static_cast<int>(settings.masterVolume * 100.0f + 0.5f));
        Memories_UWP_RequestMod("3d-monsters", settings.monsters3D ? 1 : 0);
        Memories_UWP_RequestMod("ai-hard-mode", settings.aiHardMode ? 1 : 0);
        Memories_UWP_RequestMod("drop-missing-cards", settings.dropMissingCards ? 1 : 0);
    }
}

void GameEngineBridge::QuickSave(int slot)
{
    // Slot 1..5
}

void GameEngineBridge::QuickLoad(int slot)
{
    // Slot 1..5
}

void GameEngineBridge::OnPresent(const uint32_t* pixels, int width, int height, int stride)
{
    if (!m_running || !pixels || width <= 0 || height <= 0 ||
        width > 4096 || height > 4096 || stride < width) return;

    if (!m_firstFramePresented)
    {
        m_firstFramePresented = true;
        char buf[128];
        snprintf(buf, sizeof(buf), "[BRIDGE] First frame presented successfully: %dx%d stride=%d", width, height, stride);
        UWP_LogMessage(buf);
    }

    while (m_paused && m_running)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (!m_running) return;
    // The menu may still be recording the command list when its Play callback
    // starts this thread. Transfer pixels; keep all D3D12 work on the UI thread.
    std::lock_guard<std::mutex> lock(m_frameMutex);
    m_pendingFrame.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y)
        memcpy(m_pendingFrame.data() + static_cast<size_t>(y) * width,
               pixels + static_cast<size_t>(y) * stride, width * sizeof(uint32_t));
    m_frameWidth = width;
    m_frameHeight = height;
    m_frameReady = true;

}

void GameEngineBridge::PresentPendingFrame()
{
    std::vector<uint32_t> pixels;
    int width, height;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (!m_frameReady || !m_renderer) return;
        pixels.swap(m_pendingFrame);
        width = m_frameWidth;
        height = m_frameHeight;
        m_frameReady = false;
    }
    const auto settings = GetSettings();
    m_renderer->RenderFrame(pixels.data(), width, height, width,
                            settings.widescreen, settings.bilinearFilter);
}

uint16_t GameEngineBridge::OnGetPadBits(int port)
{
    return GamepadUwp::GetGamePadBits(port);
}

bool GameEngineBridge::OnStartAudio(MemoriesAudioMixCallback mixCallback)
{
    UWP_LogMessage("[AUDIO] OnStartAudio requested");
    if (m_audio)
    {
        const bool started = m_audio->Initialize(mixCallback);
        if (started) m_audio->SetVolume(GetSettings().masterVolume);
        UWP_LogMessage(started ? "[AUDIO] OnStartAudio succeeded" : "[AUDIO] OnStartAudio failed");
        return started;
    }
    UWP_LogMessage("[AUDIO] audio backend unavailable");
    return false;
}

extern "C" {

    void UWP_OnPresent(const uint32_t* pixels, int width, int height, int stride)
    {
        GameEngineBridge::Instance().OnPresent(pixels, width, height, stride);
    }

    uint16_t UWP_OnGetPadBits(int port)
    {
        return GameEngineBridge::Instance().OnGetPadBits(port);
    }

    int UWP_IsWidescreen(void)
    {
        return GameEngineBridge::Instance().GetSettings().widescreen ? 1 : 0;
    }

    int UWP_GetSaveSlot(void)
    {
        return GameEngineBridge::Instance().GetSettings().activeSaveSlot;
    }

    int UWP_ShouldQuit(void)
    {
        return !GameEngineBridge::Instance().IsRunning();
    }

    int UWP_StartAudio(MemoriesAudioMixCallback mixCallback)
    {
        return GameEngineBridge::Instance().OnStartAudio(mixCallback) ? 1 : 0;
    }
}

void GameEngineBridge::GameThreadEntryPoint()
{
    const HRESULT apartment = RoInitialize(RO_INIT_MULTITHREADED);
    UWP_LogMessage("[BRIDGE] GameThreadEntryPoint started");
    int res = 1;
    try
    {
        res = Memories_UWP_Main(0, nullptr);
    }
    catch (Platform::Exception^ ex)
    {
        SetLastError(WideToUtf8(ex->Message->Data()));
        UWP_LogMessage(GetLastError().c_str());
    }
    catch (const std::exception& ex)
    {
        SetLastError(ex.what());
        UWP_LogMessage(ex.what());
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "[BRIDGE] Memories_UWP_Main finished with code: %d", res);
    UWP_LogMessage(buf);
    if (SUCCEEDED(apartment)) RoUninitialize();
    m_running = false;
}

