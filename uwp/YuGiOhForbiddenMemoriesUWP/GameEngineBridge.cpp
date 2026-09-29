#include "GameEngineBridge.h"
#include "GamepadUwp.h"
#include "StorageUwp.h"
#include <windows.h>
#include <fstream>
#include <string>
#include <vector>

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

extern "C" WINBASEAPI PVOID WINAPI AddVectoredExceptionHandler(ULONG First, PVECTORED_EXCEPTION_HANDLER Handler);

static LONG WINAPI CrashVectoredExceptionHandler(EXCEPTION_POINTERS *ep)
{
    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x406D1388 || code == 0x000006BA || code == 0xE06D7363 ||
        code == 0x8001010E || (code & 0xFFFF0000) == 0x80000000)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (code == EXCEPTION_ACCESS_VIOLATION ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION ||
        code == EXCEPTION_STACK_OVERFLOW ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
        code == 0xC0000028 ||
        code == 0xC0000025)
    {
        char buf[512];
        snprintf(buf, sizeof(buf),
            "[FATAL CRASH] Exception 0x%08X at RIP=0x%p, MemoryAddr=0x%p",
            code,
            ep->ExceptionRecord->ExceptionAddress,
            (void*)(ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0));
        UWP_LogMessage(buf);

        PCONTEXT ctx = ep->ContextRecord;
        if (ctx)
        {
            snprintf(buf, sizeof(buf),
                "[FATAL CRASH] RAX=0x%p RBX=0x%p RCX=0x%p RDX=0x%p RSI=0x%p RDI=0x%p RSP=0x%p RBP=0x%p",
                (void*)ctx->Rax, (void*)ctx->Rbx, (void*)ctx->Rcx, (void*)ctx->Rdx,
                (void*)ctx->Rsi, (void*)ctx->Rdi, (void*)ctx->Rsp, (void*)ctx->Rbp);
            UWP_LogMessage(buf);
            snprintf(buf, sizeof(buf),
                "[FATAL CRASH] R8=0x%p R9=0x%p R10=0x%p R11=0x%p R12=0x%p R13=0x%p R14=0x%p R15=0x%p",
                (void*)ctx->R8, (void*)ctx->R9, (void*)ctx->R10, (void*)ctx->R11,
                (void*)ctx->R12, (void*)ctx->R13, (void*)ctx->R14, (void*)ctx->R15);
            UWP_LogMessage(buf);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

GameEngineBridge::GameEngineBridge()
{
    AddVectoredExceptionHandler(1, CrashVectoredExceptionHandler);
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
    if (m_running)
    {
        StopGame();
    }

    m_renderer = renderer;
    m_romPath = romPath;
    m_settings = settings;
    m_firstFramePresented = false;
    m_lastError.clear();

    std::wstring userDir = StorageUwp::GetLocalFolderPath();

    UWP_LogMessage(("[BRIDGE] StartGame called with ROM: " + WideToUtf8(romPath)).c_str());
    UWP_LogMessage(("[BRIDGE] Local userDir: " + WideToUtf8(userDir)).c_str());

    // Set Win32 OS environment variables
    SetEnvironmentVariableW(L"MEMORIES_DISC", romPath.c_str());
    SetEnvironmentVariableW(L"MEMORIES_USER_DIR", userDir.c_str());
    SetEnvironmentVariableW(L"MEMORIES_CLOCK", L"cooperative");
    SetEnvironmentVariableW(L"MEMORIES_HEADLESS", L"0");
    SetEnvironmentVariableW(L"MEMORIES_BACKEND", L"uwp");
    SetEnvironmentVariableW(L"MEMORIES_SCALE", std::to_wstring(settings.resolutionScale).c_str());

    // Also set CRT environment variables via _wputenv so getenv() works!
    _wputenv((L"MEMORIES_DISC=" + romPath).c_str());
    _wputenv((L"MEMORIES_USER_DIR=" + userDir).c_str());
    _wputenv(L"MEMORIES_CLOCK=cooperative");
    _wputenv(L"MEMORIES_HEADLESS=0");
    _wputenv(L"MEMORIES_BACKEND=uwp");
    _wputenv((L"MEMORIES_SCALE=" + std::to_wstring(settings.resolutionScale)).c_str());

    m_running = true;
    m_paused = false;
    m_resetRequested = false;

    m_gameThread = std::thread(&GameEngineBridge::GameThreadEntryPoint, this);
    return true;
}

void GameEngineBridge::StopGame()
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
    m_settings = settings;
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
    if (!m_running || !m_renderer) return;

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
        GamepadUwp::Poll();
    }

    m_renderer->RenderFrame(pixels, width, height, stride, m_settings.widescreen, m_settings.bilinearFilter);
}

uint16_t GameEngineBridge::OnGetPadBits(int port)
{
    return GamepadUwp::GetPadBits(port);
}

bool GameEngineBridge::OnStartAudio(MemoriesAudioMixCallback mixCallback)
{
    if (m_audio)
    {
        return m_audio->Initialize(mixCallback);
    }
    return false;
}

extern "C" {
    int Memories_UWP_Main(void);

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
    UWP_LogMessage("[BRIDGE] GameThreadEntryPoint started");
    int res = Memories_UWP_Main();
    char buf[128];
    snprintf(buf, sizeof(buf), "[BRIDGE] Memories_UWP_Main finished with code: %d", res);
    UWP_LogMessage(buf);
    m_running = false;
}
