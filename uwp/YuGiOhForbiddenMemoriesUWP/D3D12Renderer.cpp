#include "D3D12Renderer.h"
#include "ImGuiFrontend.h"
#include <windows.ui.xaml.media.dxinterop.h>
#include <d3dcompiler.h>
#include <stdexcept>
#include <algorithm>
#include <vector>

using namespace Microsoft::WRL;

static const char* g_shaderSource = R"(
cbuffer TransformConstants : register(b0)
{
    float2 g_scale;
    float2 g_offset;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOutput VSMain(uint id : SV_VertexID)
{
    VSOutput output;
    /* Use two explicit triangles instead of relying on a triangle-strip
       winding after a SwapChainPanel resize. This keeps both halves of the
       4:3 image covered on PC and Xbox. */
    static const float2 positions[6] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(1.0, 1.0),
        float2(0.0, 0.0), float2(1.0, 1.0), float2(0.0, 1.0)
    };
    float2 raw_uv = positions[id];
    float2 screen_pos = raw_uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    
    // Apply aspect ratio scale and centering
    screen_pos = screen_pos * g_scale + g_offset;

    output.pos = float4(screen_pos, 0.0, 1.0);
    output.uv = raw_uv;
    return output;
}

Texture2D g_texture : register(t0);
SamplerState g_sampler : register(s0);

float4 PSMain(VSOutput input) : SV_TARGET
{
    return g_texture.Sample(g_sampler, input.uv);
}
)";

D3D12Renderer::D3D12Renderer()
{
}

D3D12Renderer::~D3D12Renderer()
{
    Shutdown();
}

bool D3D12Renderer::Initialize(IUnknown* swapChainPanel, uint32_t width, uint32_t height)
{
    m_width = (width > 0) ? width : 1920;
    m_height = (height > 0) ? height : 1080;

    if (!CreateDeviceAndQueue()) return false;
    if (!CreateSwapChain(swapChainPanel)) return false;
    if (!CreateRtvViews()) return false;
    if (!CreateRootSignatureAndPipelines()) return false;

    // Create command list
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, 
        m_commandAllocators[m_frameIndex].Get(), m_pipelineStateLinear.Get(), IID_PPV_ARGS(&m_commandList))))
    {
        return false;
    }
    m_commandList->Close();

    // Create fence
    if (FAILED(m_device->CreateFence(m_fenceValues[m_frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
    {
        return false;
    }
    m_fenceValues[m_frameIndex]++;

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) return false;

    m_initialized = true;
    ImGuiFrontend::Instance().Initialize(m_device.Get(), m_commandQueue.Get(), m_srvHeap.Get(), 1, m_width, m_height);
    return true;
}

bool D3D12Renderer::CreateDeviceAndQueue()
{
    UINT dxgiFactoryFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    // Target DirectX 12 Feature Level 12.0 for Xbox Series S / PC
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    HRESULT hr = E_FAIL;
    for (auto fl : featureLevels)
    {
        hr = D3D12CreateDevice(nullptr, fl, IID_PPV_ARGS(&m_device));
        if (SUCCEEDED(hr)) break;
    }
    if (FAILED(hr)) return false;

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
    {
        return false;
    }

    for (UINT i = 0; i < FrameCount; i++)
    {
        if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocators[i]))))
        {
            return false;
        }
    }

    return true;
}

bool D3D12Renderer::CreateSwapChain(IUnknown* panel)
{
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = m_width;
    swapChainDesc.Height = m_height;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain1;
    if (FAILED(factory->CreateSwapChainForComposition(m_commandQueue.Get(), &swapChainDesc, nullptr, &swapChain1)))
    {
        return false;
    }

    if (FAILED(swapChain1.As(&m_swapChain))) return false;

    if (panel)
    {
        ComPtr<ISwapChainPanelNative> panelNative;
        if (SUCCEEDED(panel->QueryInterface(IID_PPV_ARGS(&panelNative))))
        {
            panelNative->SetSwapChain(m_swapChain.Get());
        }
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    return true;
}

bool D3D12Renderer::CreateRtvViews()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)))) return false;

    UINT rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT n = 0; n < FrameCount; n++)
    {
        if (FAILED(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])))) return false;
        m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += rtvDescriptorSize;
    }

    // SRV heap for game texture, ImGui font, and logo. Keep this heap across
    // swap-chain resizes: ImGui and the logo retain descriptor handles into it.
    if (!m_srvHeap)
    {
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 16;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)))) return false;
    }

    return true;
}

bool D3D12Renderer::CreateRootSignatureAndPipelines()
{
    // Root Signature: 4 Root 32-bit constants (b0) + 1 Descriptor Table (t0) + 1 Static Sampler (s0)
    D3D12_DESCRIPTOR_RANGE descriptorRanges[1] = {};
    descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    descriptorRanges[0].NumDescriptors = 1;
    descriptorRanges[0].BaseShaderRegister = 0;
    descriptorRanges[0].RegisterSpace = 0;
    descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    // Param 0: 4 32-bit constants for scale_x, scale_y, offset_x, offset_y
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    rootParameters[0].Constants.ShaderRegister = 0;
    rootParameters[0].Constants.RegisterSpace = 0;
    rootParameters[0].Constants.Num32BitValues = 4;

    // Param 1: Texture SRV
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &descriptorRanges[0];

    // Static Samplers: Point and Linear
    D3D12_STATIC_SAMPLER_DESC samplerDescPoint = {};
    samplerDescPoint.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplerDescPoint.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDescPoint.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDescPoint.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDescPoint.ShaderRegister = 0;
    samplerDescPoint.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplerDescLinear = samplerDescPoint;
    samplerDescLinear.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;

    D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
    rootSigDesc.NumParameters = 2;
    rootSigDesc.pParameters = rootParameters;
    rootSigDesc.NumStaticSamplers = 1;
    rootSigDesc.pStaticSamplers = &samplerDescLinear;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    if (FAILED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error)))
    {
        return false;
    }

    if (FAILED(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
    {
        return false;
    }

    // Compile Shaders
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    if (FAILED(D3DCompile(g_shaderSource, strlen(g_shaderSource), "shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &error)))
    {
        return false;
    }
    if (FAILED(D3DCompile(g_shaderSource, strlen(g_shaderSource), "shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psBlob, &error)))
    {
        return false;
    }

    // Pipeline State
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    // Linear pipeline
    if (FAILED(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateLinear))))
    {
        return false;
    }

    // Point pipeline
    rootSigDesc.pStaticSamplers = &samplerDescPoint;
    if (SUCCEEDED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, nullptr)))
    {
        ComPtr<ID3D12RootSignature> rootSigPoint;
        if (SUCCEEDED(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSigPoint))))
        {
            psoDesc.pRootSignature = rootSigPoint.Get();
            m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStatePoint));
        }
    }
    if (!m_pipelineStatePoint) m_pipelineStatePoint = m_pipelineStateLinear;

    return true;
}

bool D3D12Renderer::EnsureTexture(uint32_t width, uint32_t height)
{
    if (m_gameTexture && m_textureWidth == width && m_textureHeight == height)
    {
        return true;
    }

    m_textureWidth = width;
    m_textureHeight = height;
    m_texturePitch = (width * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    texDesc.DepthOrArraySize = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    D3D12_HEAP_PROPERTIES defaultHeap = { D3D12_HEAP_TYPE_DEFAULT };
    if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_gameTexture))))
    {
        return false;
    }

    UINT64 uploadSize = m_texturePitch * height;
    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeap = { D3D12_HEAP_TYPE_UPLOAD };
    if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_uploadBuffer))))
    {
        return false;
    }

    // Create SRV in descriptor heap
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_gameTexture.Get(), &srvDesc, m_srvHeap->GetCPUDescriptorHandleForHeapStart());

    return true;
}

void D3D12Renderer::RenderFrame(const uint32_t* pixels, int width, int height, int stride, bool widescreen, bool bilinear)
{
    if (!m_initialized || !pixels || width <= 0 || height <= 0) return;

    // There is one upload buffer and one game texture shared by all frames.
    // Wait before CPU writes can overwrite a still-pending GPU copy.
    WaitForGpu();
    if (!EnsureTexture(width, height)) return;

    // Upload pixel data to upload buffer
    void* pData = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (SUCCEEDED(m_uploadBuffer->Map(0, &readRange, &pData)))
    {
        const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(pixels);
        uint8_t* dstBytes = reinterpret_cast<uint8_t*>(pData);
        uint32_t srcPitch = (stride > 0) ? (stride * sizeof(uint32_t)) : (width * sizeof(uint32_t));
        uint32_t copyBytesPerRow = width * sizeof(uint32_t);

        for (int y = 0; y < height; y++)
        {
            memcpy(dstBytes + y * m_texturePitch, srcBytes + y * srcPitch, copyBytesPerRow);
        }
        m_uploadBuffer->Unmap(0, nullptr);
    }

    // Record commands
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), 
        bilinear ? m_pipelineStateLinear.Get() : m_pipelineStatePoint.Get());

    // Copy upload buffer to game texture
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = m_gameTexture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_uploadBuffer.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint.Offset = 0;
    srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srcLoc.PlacedFootprint.Footprint.Width = width;
    srcLoc.PlacedFootprint.Footprint.Height = height;
    srcLoc.PlacedFootprint.Footprint.Depth = 1;
    srcLoc.PlacedFootprint.Footprint.RowPitch = m_texturePitch;

    m_commandList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    // Barrier game texture to pixel shader resource
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = m_gameTexture.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    // Barrier back buffer to render target
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(2, barriers);

    // Set Viewport and Scissor
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)m_width, (LONG)m_height };
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    // Clear Render Target to dark PlayStation black
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    UINT rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    rtvHandle.ptr += m_frameIndex * rtvDescriptorSize;

    const float clearColor[] = { 0.05f, 0.05f, 0.08f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Set Root Signature and Descriptor Heaps
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
    ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, ppHeaps);
    m_commandList->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    // Calculate Aspect Ratio: 4:3 Pillarbox or 16:9 Fullscreen
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (!widescreen)
    {
        // 4:3 on 16:9 display
        float screenAspect = (float)m_width / (float)m_height;
        float targetAspect = 4.0f / 3.0f;
        if (screenAspect > targetAspect)
        {
            scaleX = targetAspect / screenAspect;
        }
        else
        {
            scaleY = screenAspect / targetAspect;
        }
    }

    float constants[4] = { scaleX, scaleY, 0.0f, 0.0f };
    m_commandList->SetGraphicsRoot32BitConstants(0, 4, constants, 0);

    // Draw Quad: two explicit triangles.
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(6, 1, 0, 0);

    // Render in-game ImGui overlay if open
    if (ImGuiFrontend::Instance().IsInGameMenuOpen())
    {
        bool open = true;
        ImGuiFrontend::Instance().NewFrame();
        ImGuiFrontend::Instance().DrawInGameOverlay(&open, nullptr, nullptr, nullptr, nullptr, nullptr);
        ImGuiFrontend::Instance().SetInGameMenuOpen(open);
        ImGuiFrontend::Instance().Render(m_commandList.Get());
    }

    // Transition backbuffer to PRESENT and texture back to COPY_DEST
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(2, barriers);

    m_commandList->Close();

    // Execute Command List
    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

    // Present (1 = VSync on Xbox Series S 60 Hz)
    m_swapChain->Present(1, 0);

    MoveToNextFrame();
}

void D3D12Renderer::MoveToNextFrame()
{
    const UINT64 currentFenceValue = m_fenceValues[m_frameIndex];
    m_commandQueue->Signal(m_fence.Get(), currentFenceValue);

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
    }

    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
}

void D3D12Renderer::WaitForGpu()
{
    if (m_commandQueue && m_fence && m_fenceEvent)
    {
        m_commandQueue->Signal(m_fence.Get(), m_fenceValues[m_frameIndex]);
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
        m_fenceValues[m_frameIndex]++;
    }
}

void D3D12Renderer::Resize(uint32_t width, uint32_t height)
{
    if (!m_initialized || (m_width == width && m_height == height) || width == 0 || height == 0) return;

    WaitForGpu();

    for (UINT i = 0; i < FrameCount; i++)
    {
        m_renderTargets[i].Reset();
        m_fenceValues[i] = m_fenceValues[m_frameIndex];
    }

    m_width = width;
    m_height = height;

    DXGI_SWAP_CHAIN_DESC desc = {};
    m_swapChain->GetDesc(&desc);
    if (FAILED(m_swapChain->ResizeBuffers(FrameCount, width, height, desc.BufferDesc.Format, desc.Flags)))
    {
        return;
    }

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    if (!CreateRtvViews()) return;
    ImGuiFrontend::Instance().Resize(width, height);
}

void D3D12Renderer::Shutdown()
{
    if (m_initialized)
    {
        WaitForGpu();
        ImGuiFrontend::Instance().Shutdown();
        if (m_fenceEvent)
        {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
        m_initialized = false;
    }
}

void D3D12Renderer::RenderMenu(const std::wstring& activeRomPath, bool isRomValid,
                               const std::function<void()>& onSelectRom,
                               const std::function<void()>& onPlayGame)
{
    if (!m_initialized) return;

    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);

    // Barrier back buffer to render target
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    // Set Viewport and Scissor
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, (LONG)m_width, (LONG)m_height };
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    // Clear Render Target to dark #05030A
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    UINT rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    rtvHandle.ptr += m_frameIndex * rtvDescriptorSize;

    const float clearColor[] = { 0.02f, 0.01f, 0.04f, 1.0f };
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, ppHeaps);

    // Render ImGui Menu
    ImGuiFrontend::Instance().NewFrame();
    ImGuiFrontend::Instance().DrawMainMenu(activeRomPath, isRomValid, onSelectRom, onPlayGame);
    ImGuiFrontend::Instance().Render(m_commandList.Get());

    // Transition back buffer back to PRESENT
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();

    ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, ppCommandLists);

    m_swapChain->Present(1, 0);
    MoveToNextFrame();
}

void D3D12Renderer::RenderInGameOverlay(const std::function<void()>& onResume,
                                        const std::function<void()>& onQuickSave,
                                        const std::function<void()>& onQuickLoad,
                                        const std::function<void()>& onReset,
                                        const std::function<void()>& onQuit)
{
    if (!m_initialized || !ImGuiFrontend::Instance().IsInGameMenuOpen()) return;

    ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, ppHeaps);

    bool open = true;
    ImGuiFrontend::Instance().NewFrame();
    ImGuiFrontend::Instance().DrawInGameOverlay(&open, onResume, onQuickSave, onQuickLoad, onReset, onQuit);
    ImGuiFrontend::Instance().SetInGameMenuOpen(open);
    ImGuiFrontend::Instance().Render(m_commandList.Get());
}
