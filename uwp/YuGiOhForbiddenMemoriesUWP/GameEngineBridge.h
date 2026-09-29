#pragma once

#include "D3D12Renderer.h"
#include "AudioXAudio2.h"
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>

struct GameSettings
{
    int resolutionScale = 4; // 1x, 2x, 4x
    bool widescreen = false;  // 4:3 vs 16:9
    bool bilinearFilter = true;
    bool hdText = true;
    bool monsters3D = true;
    bool aiHardMode = false;
    bool dropMissingCards = true;
    int activeSaveSlot = 1;
};

class GameEngineBridge
{
public:
    static GameEngineBridge& Instance();

    bool StartGame(D3D12Renderer* renderer, const std::wstring& romPath, const GameSettings& settings);
    void StopGame();
    void PauseGame();
    void ResumeGame();
    void RequestReset();

    bool IsRunning() const { return m_running; }
    bool IsPaused() const { return m_paused; }

    void SetSettings(const GameSettings& settings);
    const GameSettings& GetSettings() const { return m_settings; }

    void QuickSave(int slot);
    void QuickLoad(int slot);

    std::string GetLastError() const { return m_lastError; }
    void SetLastError(const std::string& err) { m_lastError = err; }
    bool HasFirstFramePresented() const { return m_firstFramePresented; }

    // Callbacks from platform layer
    void OnPresent(const uint32_t* pixels, int width, int height, int stride);
    uint16_t OnGetPadBits(int port);
    bool OnStartAudio(MemoriesAudioMixCallback mixCallback);

private:
    GameEngineBridge();
    ~GameEngineBridge();

    D3D12Renderer* m_renderer = nullptr;
    std::unique_ptr<AudioXAudio2> m_audio;

    GameSettings m_settings;
    std::wstring m_romPath;
    std::string m_lastError;

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_paused{ false };
    std::atomic<bool> m_resetRequested{ false };
    std::atomic<bool> m_firstFramePresented{ false };

    std::thread m_gameThread;

    void GameThreadEntryPoint();
};
