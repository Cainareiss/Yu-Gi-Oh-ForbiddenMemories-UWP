#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <string>
#include <functional>
#include <memory>
#include "StorageUwp.h"
#include "FrontendGamepadInput.h"

struct FrontendSettings
{
    int resolutionScale = 1;     // 1 = 1x, 2 = 2x (1080p), 4 = 4x (4K)
    bool widescreen = false;      // false = 4:3, true = 16:9
    bool bilinearFilter = true;  // true = linear, false = point
    bool hdText = true;
    bool monsters3D = true;
    bool aiHardMode = false;
    bool dropMissingCards = false;
    float masterVolume = 1.0f;
};

class ImGuiFrontend
{
public:
    static ImGuiFrontend& Instance();

    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue, 
                    ID3D12DescriptorHeap* srvHeap, UINT srvIndex,
                    uint32_t width, uint32_t height);
    void Shutdown();
    void Resize(uint32_t width, uint32_t height);

    // Input events
    void OnPointerPressed(float x, float y, int button);
    void OnPointerReleased(float x, float y, int button);
    void OnPointerMoved(float x, float y);
    void OnPointerWheel(float delta);
    void OnKeyDown(int virtualKey);
    void OnKeyUp(int virtualKey);
    void OnCharacter(wchar_t ch);
    void UpdateGamepad(uint16_t padBits, bool connected, bool inGame);
    void ResetInput();

    // Frame lifecycle
    void NewFrame();
    void Render(ID3D12GraphicsCommandList* commandList);

    // Main Menu following "E:\Sem tÃ­tulo.svg"
    void DrawMainMenu(const std::wstring& activeRomPath, bool isRomValid,
                      const std::function<void()>& onSelectRom,
                      const std::function<void()>& onPlayGame);

    // In-game pause overlay
    void DrawInGameOverlay(bool* pOpen,
                           const std::function<void()>& onResume,
                           const std::function<void()>& onQuickSave,
                           const std::function<void()>& onQuickLoad,
                           const std::function<void()>& onReset,
                           const std::function<void()>& onQuit);

    FrontendSettings& GetSettings() { return m_settings; }
    void SetSettings(const FrontendSettings& s) { m_settings = s; }

    bool IsInGameMenuOpen() const { return m_inGameMenuOpen; }
    void SetInGameMenuOpen(bool open) { m_inGameMenuOpen = open; }
    void ToggleInGameMenu() { m_inGameMenuOpen = !m_inGameMenuOpen; }
    bool HandleBackNavigation()
    {
        if (m_showSettingsModal) { m_showSettingsModal = false; return true; }
        if (m_showModsModal) { m_showModsModal = false; return true; }
        if (m_inGameMenuOpen) { m_inGameMenuOpen = false; return true; }
        return false;
    }

private:
    ImGuiFrontend();
    ~ImGuiFrontend();

    void ApplyEgyptianTheme();
    bool LoadLogoTexture();
    void DrawEgyptianDivider(float centerX, float centerY, float width);

    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_commandQueue = nullptr;
    ID3D12DescriptorHeap* m_srvHeap = nullptr;
    UINT m_srvIndex = 1;

    uint32_t m_width = 1920;
    uint32_t m_height = 1080;
    bool m_initialized = false;

    // Logo texture in D3D12
    Microsoft::WRL::ComPtr<ID3D12Resource> m_logoTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_logoUploadBuffer;
    D3D12_GPU_DESCRIPTOR_HANDLE m_logoGpuHandle = {};
    bool m_logoLoaded = false;
    int m_logoWidth = 0;
    int m_logoHeight = 0;

    // Modals
    bool m_showModsModal = false;
    bool m_showSettingsModal = false;
    bool m_inGameMenuOpen = false;

    FrontendSettings m_settings;

    // Gamepad state tracking
    FrontendGamepadInput m_gamepadInput;
    bool m_focusMainMenu = true;
    bool m_focusInGameMenu = true;
};
