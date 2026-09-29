#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <memory>
#include <string>
#include <functional>

class D3D12Renderer
{
public:
    D3D12Renderer();
    ~D3D12Renderer();

    bool Initialize(IUnknown* swapChainPanel, uint32_t width, uint32_t height);
    void Resize(uint32_t width, uint32_t height);
    void RenderFrame(const uint32_t* pixels, int width, int height, int stride, bool widescreen, bool bilinear);
    void RenderMenu(const std::wstring& activeRomPath, bool isRomValid,
                    const std::function<void()>& onSelectRom,
                    const std::function<void()>& onPlayGame);
    void RenderInGameOverlay(const std::function<void()>& onResume,
                             const std::function<void()>& onQuickSave,
                             const std::function<void()>& onQuickLoad,
                             const std::function<void()>& onReset,
                             const std::function<void()>& onQuit);
    void Shutdown();

    uint32_t GetWidth() const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

private:
    static const UINT FrameCount = 3; // Triple buffering for optimal Xbox Series S latency/presentation

    void WaitForGpu();
    void MoveToNextFrame();
    bool CreateDeviceAndQueue();
    bool CreateSwapChain(IUnknown* panel);
    bool CreateRtvViews();
    bool CreateRootSignatureAndPipelines();
    bool EnsureTexture(uint32_t width, uint32_t height);

    uint32_t m_width = 1920;
    uint32_t m_height = 1080;
    UINT m_frameIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_commandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_commandAllocators[FrameCount];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineStatePoint;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipelineStateLinear;

    // Game texture and upload buffer
    Microsoft::WRL::ComPtr<ID3D12Resource> m_gameTexture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_uploadBuffer;
    uint32_t m_textureWidth = 0;
    uint32_t m_textureHeight = 0;
    uint32_t m_texturePitch = 0;

    // Synchronization objects
    HANDLE m_fenceEvent = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValues[FrameCount] = { 0 };

    bool m_initialized = false;
};
