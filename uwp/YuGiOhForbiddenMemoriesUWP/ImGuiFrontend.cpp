#define _CRT_SECURE_NO_WARNINGS
#include "ImGuiFrontend.h"
#include "imgui.h"
#include "backends/imgui_impl_dx12.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#include <vector>
#include <algorithm>
#include <cmath>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

static void SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
{
    // The main renderer reserves descriptor 1 for ImGui fonts
    ID3D12Device* device = info->Device;
    UINT incSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = info->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = info->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += incSize * 1;
    gpu.ptr += incSize * 1;
    *out_cpu = cpu;
    *out_gpu = gpu;
}

static void SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
{
    (void)info; (void)cpu; (void)gpu;
}

ImGuiFrontend& ImGuiFrontend::Instance()
{
    static ImGuiFrontend s_instance;
    return s_instance;
}

ImGuiFrontend::ImGuiFrontend()
{
}

ImGuiFrontend::~ImGuiFrontend()
{
    Shutdown();
}

bool ImGuiFrontend::Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue,
                               ID3D12DescriptorHeap* srvHeap, UINT srvIndex,
                               uint32_t width, uint32_t height)
{
    if (m_initialized) return true;

    m_device = device;
    m_commandQueue = commandQueue;
    m_srvHeap = srvHeap;
    m_srvIndex = srvIndex;
    m_width = width;
    m_height = height;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigNavMoveSetMousePos = false;
    io.ConfigNavEscapeClearFocusItem = false;
    io.ConfigNavCursorVisibleAlways = true;
    io.MouseDrawCursor = false;
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2((float)width, (float)height);

    ApplyEgyptianTheme();

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device;
    initInfo.CommandQueue = commandQueue;
    initInfo.NumFramesInFlight = 3;
    initInfo.RTVFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = srvHeap;
    initInfo.SrvDescriptorAllocFn = SrvDescriptorAlloc;
    initInfo.SrvDescriptorFreeFn = SrvDescriptorFree;

    if (!ImGui_ImplDX12_Init(&initInfo))
    {
        return false;
    }

    LoadLogoTexture();

    m_initialized = true;
    return true;
}

void ImGuiFrontend::Shutdown()
{
    if (!m_initialized) return;

    ImGui_ImplDX12_Shutdown();
    ImGui::DestroyContext();

    m_logoTexture.Reset();
    m_logoUploadBuffer.Reset();
    m_initialized = false;
}

void ImGuiFrontend::Resize(uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;
    if (m_initialized)
    {
        ImGui::GetIO().DisplaySize = ImVec2((float)width, (float)height);
    }
}

void ImGuiFrontend::ApplyEgyptianTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding     = ImVec2(24.0f, 24.0f);
    style.FramePadding      = ImVec2(16.0f, 10.0f);
    style.ItemSpacing       = ImVec2(12.0f, 14.0f);
    style.ItemInnerSpacing  = ImVec2(10.0f, 8.0f);
    style.WindowRounding    = 12.0f;
    style.FrameRounding     = 8.0f;
    style.PopupRounding     = 10.0f;
    style.GrabRounding      = 6.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 1.5f;
    style.PopupBorderSize   = 2.0f;

    ImVec4* colors = style.Colors;
    // Deep Egyptian royal dark palette (#05030A background)
    colors[ImGuiCol_WindowBg]             = ImVec4(0.02f, 0.01f, 0.04f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.08f, 0.05f, 0.14f, 0.98f);
    colors[ImGuiCol_Border]               = ImVec4(0.16f, 0.12f, 0.26f, 1.00f); // #2A1F42
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
    colors[ImGuiCol_Text]                 = ImVec4(0.63f, 0.63f, 0.67f, 1.00f); // #A1A1AB
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.40f, 0.38f, 0.45f, 1.00f);

    // Standard Buttons (#0E0A1A with #2A1F42 border)
    colors[ImGuiCol_Button]               = ImVec4(0.055f, 0.039f, 0.102f, 0.75f);
    colors[ImGuiCol_ButtonHovered]        = ImVec4(0.15f, 0.09f, 0.25f, 0.90f);
    colors[ImGuiCol_ButtonActive]         = ImVec4(0.22f, 0.13f, 0.35f, 1.00f);

    // Egyptian Gold highlights (#D4AF37 / #FFD700)
    colors[ImGuiCol_Header]               = ImVec4(0.18f, 0.13f, 0.28f, 0.80f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.30f, 0.22f, 0.45f, 0.90f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.40f, 0.30f, 0.60f, 1.00f);
    colors[ImGuiCol_CheckMark]            = ImVec4(0.83f, 0.69f, 0.22f, 1.00f);
    colors[ImGuiCol_SliderGrab]           = ImVec4(0.83f, 0.69f, 0.22f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]     = ImVec4(1.00f, 0.84f, 0.00f, 1.00f);
    colors[ImGuiCol_FrameBg]              = ImVec4(0.08f, 0.05f, 0.14f, 0.80f);
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.14f, 0.09f, 0.24f, 0.90f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.20f, 0.13f, 0.32f, 1.00f);
    colors[ImGuiCol_NavHighlight]         = ImVec4(0.83f, 0.69f, 0.22f, 1.00f);
}

bool ImGuiFrontend::LoadLogoTexture()
{
    // Try to load extracted logo_design.png from package assets
    std::wstring assetPath = L"Assets\\logo_design.png";
    FILE* f = nullptr;
    if (_wfopen_s(&f, assetPath.c_str(), L"rb") != 0 || !f)
    {
        // Try fallback to LocalFolder or relative
        std::wstring fallback = StorageUwp::GetLocalFolderPath() + L"\\logo_design.png";
        if (_wfopen_s(&f, fallback.c_str(), L"rb") != 0 || !f)
        {
            return false;
        }
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fileSize <= 0)
    {
        fclose(f);
        return false;
    }

    std::vector<unsigned char> fileData(fileSize);
    fread(fileData.data(), 1, fileSize, f);
    fclose(f);

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(fileData.data(), (int)fileSize, &width, &height, &channels, 4);
    if (!pixels)
    {
        return false;
    }

    m_logoWidth = width;
    m_logoHeight = height;

    // Create D3D12 committed texture for logo
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_logoTexture))))
    {
        stbi_image_free(pixels);
        return false;
    }

    // Calculate footprint and create upload buffer
    UINT64 uploadBufferSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT numRows = 0;
    UINT64 rowSizeInBytes = 0;
    UINT64 totalBytes = 0;
    m_device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes);
    uploadBufferSize = totalBytes;

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadBufferSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (FAILED(m_device->CreateCommittedResource(
        &uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_logoUploadBuffer))))
    {
        stbi_image_free(pixels);
        return false;
    }

    // Copy pixels to upload buffer
    BYTE* pUploadData = nullptr;
    m_logoUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pUploadData));
    for (int y = 0; y < height; y++)
    {
        memcpy(pUploadData + footprint.Offset + y * footprint.Footprint.RowPitch,
               pixels + y * width * 4,
               width * 4);
    }
    m_logoUploadBuffer->Unmap(0, nullptr);
    stbi_image_free(pixels);

    // Create temporary command allocator & list for one-time texture upload
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> cmdAlloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmdList;
    if (SUCCEEDED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdAlloc))) &&
        SUCCEEDED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmdAlloc.Get(), nullptr, IID_PPV_ARGS(&cmdList))))
    {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = m_logoTexture.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = m_logoUploadBuffer.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprint;

        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_logoTexture.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmdList->ResourceBarrier(1, &barrier);

        cmdList->Close();

        ID3D12CommandList* lists[] = { cmdList.Get() };
        m_commandQueue->ExecuteCommandLists(1, lists);

        // Wait for upload execution
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
        HANDLE fenceEv = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        m_commandQueue->Signal(fence.Get(), 1);
        fence->SetEventOnCompletion(1, fenceEv);
        WaitForSingleObject(fenceEv, 5000);
        CloseHandle(fenceEv);
    }

    // Create SRV in descriptor heap at index 2 (index 0 is game texture, index 1 is ImGui fonts, index 2 is logo)
    UINT incSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.ptr += incSize * 2;
    m_logoGpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    m_logoGpuHandle.ptr += incSize * 2;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->CreateShaderResourceView(m_logoTexture.Get(), &srvDesc, cpuHandle);
    m_logoLoaded = true;
    return true;
}

void ImGuiFrontend::OnPointerPressed(float x, float y, int button)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    if (button >= 0 && button < 5)
    {
        io.AddMouseButtonEvent(button, true);
    }
}

void ImGuiFrontend::OnPointerReleased(float x, float y, int button)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    if (button >= 0 && button < 5)
    {
        io.AddMouseButtonEvent(button, false);
    }
}

void ImGuiFrontend::OnPointerMoved(float x, float y)
{
    ImGui::GetIO().AddMousePosEvent(x, y);
}

void ImGuiFrontend::OnPointerWheel(float delta)
{
    ImGui::GetIO().AddMouseWheelEvent(0.0f, delta);
}

void ImGuiFrontend::OnKeyDown(int virtualKey)
{
    ImGuiIO& io = ImGui::GetIO();
    // Map standard virtual keys
    if (virtualKey == 0x26) io.AddKeyEvent(ImGuiKey_UpArrow, true);
    if (virtualKey == 0x28) io.AddKeyEvent(ImGuiKey_DownArrow, true);
    if (virtualKey == 0x25) io.AddKeyEvent(ImGuiKey_LeftArrow, true);
    if (virtualKey == 0x27) io.AddKeyEvent(ImGuiKey_RightArrow, true);
    if (virtualKey == 0x0D) io.AddKeyEvent(ImGuiKey_Enter, true);
    if (virtualKey == 0x1B) io.AddKeyEvent(ImGuiKey_Escape, true);
    if (virtualKey == 0x20) io.AddKeyEvent(ImGuiKey_Space, true);
}

void ImGuiFrontend::OnKeyUp(int virtualKey)
{
    ImGuiIO& io = ImGui::GetIO();
    if (virtualKey == 0x26) io.AddKeyEvent(ImGuiKey_UpArrow, false);
    if (virtualKey == 0x28) io.AddKeyEvent(ImGuiKey_DownArrow, false);
    if (virtualKey == 0x25) io.AddKeyEvent(ImGuiKey_LeftArrow, false);
    if (virtualKey == 0x27) io.AddKeyEvent(ImGuiKey_RightArrow, false);
    if (virtualKey == 0x0D) io.AddKeyEvent(ImGuiKey_Enter, false);
    if (virtualKey == 0x1B) io.AddKeyEvent(ImGuiKey_Escape, false);
    if (virtualKey == 0x20) io.AddKeyEvent(ImGuiKey_Space, false);
}

void ImGuiFrontend::OnCharacter(wchar_t ch)
{
    ImGui::GetIO().AddInputCharacter((unsigned int)ch);
}

void ImGuiFrontend::UpdateGamepad(uint16_t padBits, bool connected, bool inGame)
{
    if (!m_initialized) return;
    if (m_gamepadInput.Update(padBits, connected, inGame, m_inGameMenuOpen))
    {
        ToggleInGameMenu();
        m_focusInGameMenu = m_inGameMenuOpen;
    }
    if (inGame) m_focusMainMenu = true;
}

void ImGuiFrontend::NewFrame()
{
    if (!m_initialized) return;
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
}

void ImGuiFrontend::Render(ID3D12GraphicsCommandList* commandList)
{
    if (!m_initialized) return;
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

void ImGuiFrontend::DrawEgyptianDivider(float centerX, float centerY, float width)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float scale = std::max(0.60f, std::min(1.0f, width / 360.0f));
    float halfWidth = width * 0.5f;

    // Left gold bar with gradient opacity (#AA8833)
    drawList->AddLine(
        ImVec2(centerX - halfWidth, centerY),
        ImVec2(centerX - 35.0f * scale, centerY),
        IM_COL32(170, 136, 51, 140), 2.0f);

    // Center Egyptian Millennium / Eye of Horus Emblem in #D4AF37
    // Inverted Triangle (Millennium Puzzle shape)
    ImVec2 pTopLeft(centerX - 16.0f * scale, centerY - 8.0f * scale);
    ImVec2 pTopRight(centerX + 16.0f * scale, centerY - 8.0f * scale);
    ImVec2 pBottom(centerX, centerY + 12.0f * scale);
    drawList->AddTriangle(pTopLeft, pTopRight, pBottom, IM_COL32(212, 175, 55, 230), 2.0f * scale);

    // Eye arc and pupil
    drawList->AddCircleFilled(ImVec2(centerX, centerY - 1.0f * scale), 3.0f * scale, IM_COL32(255, 215, 0, 255));
    drawList->AddLine(ImVec2(centerX - 9.0f * scale, centerY - 3.0f * scale), ImVec2(centerX + 9.0f * scale, centerY - 3.0f * scale), IM_COL32(212, 175, 55, 200), 1.5f * scale);

    // Right gold bar with gradient opacity (#AA8833)
    drawList->AddLine(
        ImVec2(centerX + 35.0f * scale, centerY),
        ImVec2(centerX + halfWidth, centerY),
        IM_COL32(170, 136, 51, 140), 2.0f);
}

static void DrawGoldStar(ImDrawList* drawList, ImVec2 center, float radius)
{
    ImVec2 points[10];
    for (int i = 0; i < 10; ++i)
    {
        const float angle = -3.14159265358979323846f * 0.5f +
                            static_cast<float>(i) * 3.14159265358979323846f / 5.0f;
        const float pointRadius = (i & 1) ? radius * 0.42f : radius;
        points[i] = ImVec2(center.x + std::cos(angle) * pointRadius,
                            center.y + std::sin(angle) * pointRadius);
    }
    drawList->AddConvexPolyFilled(points, 10, IM_COL32(255, 215, 0, 255));
    drawList->AddPolyline(points, 10, IM_COL32(212, 175, 55, 255), ImDrawFlags_Closed, 1.0f);
}

void ImGuiFrontend::DrawMainMenu(const std::wstring& activeRomPath, bool isRomValid,
                                 const std::function<void()>& onSelectRom,
                                 const std::function<void()>& onPlayGame)
{
    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y;
    float centerX = screenW * 0.5f;

    // Fullscreen seamless canvas
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(screenW, screenH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    if (m_focusMainMenu) ImGui::SetNextWindowFocus();
    ImGui::Begin("##MainMenuCanvas", nullptr, flags);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // 1. Draw Background: Deep Egyptian Violet/Black (#05030A)
    drawList->AddRectFilled(ImVec2(0, 0), ImVec2(screenW, screenH), IM_COL32(5, 3, 10, 255));

    // 2. Draw Subtle Radial Egyptian Ambient Lighting (#1B0D2D glow centered near logo)
    ImVec2 glowCenter(centerX, screenH * 0.32f);
    float maxRadius = screenW * 0.45f;
    for (int step = 6; step >= 1; step--)
    {
        float r = maxRadius * (step / 6.0f);
        int alpha = (int)(22.0f * (1.0f - (float)step / 7.0f));
        drawList->AddCircleFilled(glowCenter, r, IM_COL32(27, 13, 45, alpha));
    }

    // 3. Responsive centered layout. Base all coordinates on the actual
    // SwapChainPanel size so the settings button remains visible at 600p.
    const float layoutScale = std::max(0.60f, std::min(1.0f, (screenH - 24.0f) / 620.0f));
    const float horizontalMargin = 24.0f;
    const float logoW = std::min(400.0f * layoutScale, screenW - horizontalMargin * 2.0f);
    const float logoH = m_logoLoaded ? logoW * 0.75f : 190.0f * layoutScale;
    const float dividerGap = 10.0f * layoutScale;
    const float startGap = 24.0f * layoutScale;
    const float btnW = std::min(360.0f * layoutScale, screenW - horizontalMargin * 2.0f);
    const float btnH = std::max(40.0f, 52.0f * layoutScale);
    const float buttonGap = std::max(8.0f, 14.0f * layoutScale);
    const float statusGap = 16.0f * layoutScale;
    const float statusHeight = 18.0f * layoutScale;
    const float contentHeight = logoH + dividerGap + startGap + btnH * 4.0f +
                                buttonGap * 3.0f + statusGap + statusHeight;
    const float logoY = std::max(12.0f, (screenH - contentHeight) * 0.5f);
    if (m_logoLoaded)
    {
        // Render 400x300 logo from SVG design
        ImVec2 logoPos(centerX - logoW * 0.5f, logoY);
        drawList->AddImage((ImTextureID)m_logoGpuHandle.ptr,
                           logoPos,
                           ImVec2(logoPos.x + logoW, logoPos.y + logoH));
    }
    else
    {
        // Elegant typography fallback matching design
        const char* title = "YU-GI-OH!";
        const char* sub = "FORBIDDEN MEMORIES";
        const char* tag = "DIRECTX 12 • RECOMPILED";
        ImGui::SetWindowFontScale(2.4f);
        ImVec2 tSize = ImGui::CalcTextSize(title);
        drawList->AddText(ImVec2(centerX - tSize.x * 0.5f, logoY + 40.0f), IM_COL32(255, 215, 0, 255), title);

        ImGui::SetWindowFontScale(1.8f);
        ImVec2 sSize = ImGui::CalcTextSize(sub);
        drawList->AddText(ImVec2(centerX - sSize.x * 0.5f, logoY + 110.0f), IM_COL32(255, 255, 255, 255), sub);

        ImGui::SetWindowFontScale(1.0f);
        ImVec2 tagSize = ImGui::CalcTextSize(tag);
        drawList->AddText(ImVec2(centerX - tagSize.x * 0.5f, logoY + 165.0f), IM_COL32(212, 175, 55, 220), tag);
    }

    // 4. Ornamental Egyptian Gold Divider at y ~ 429
    const float dividerY = logoY + logoH + dividerGap;
    DrawEgyptianDivider(centerX, dividerY,
                        std::min(360.0f * layoutScale, screenW - horizontalMargin * 2.0f));

    // 5. Four main action buttons, all inside the visible panel.
    const float startY = dividerY + startGap;
    const float btnX = centerX - btnW * 0.5f;

    // --- BUTTON 1: SELECIONAR JOGO ---
    ImGui::SetCursorPos(ImVec2(btnX, startY));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(14.f/255.f, 10.f/255.f, 26.f/255.f, 0.70f)); // #0E0A1A
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(42.f/255.f, 31.f/255.f, 66.f/255.f, 1.00f)); // #2A1F42
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(161.f/255.f, 161.f/255.f, 171.f/255.f, 1.0f)); // #A1A1AB
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);

    if (ImGui::Button("SELECIONAR JOGO", ImVec2(btnW, btnH)))
    {
        if (onSelectRom) onSelectRom();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // --- BUTTON 2: INICIAR JOGO (gold anime stars are drawn geometrically) ---
    const float btn2Y = startY + btnH + buttonGap;
    ImGui::SetCursorPos(ImVec2(btnX, btn2Y));

    // Gold outer glow drop-shadow behind INICIAR JOGO
    drawList->AddRect(
        ImVec2(btnX - 2.0f, btn2Y - 2.0f),
        ImVec2(btnX + btnW + 2.0f, btn2Y + btnH + 2.0f),
        IM_COL32(212, 175, 55, 60), 10.0f, 0, 4.0f);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(31.f/255.f, 20.f/255.f, 48.f/255.f, 0.90f)); // #1F1430
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(50.f/255.f, 32.f/255.f, 75.f/255.f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(212.f/255.f, 175.f/255.f, 55.f/255.f, 1.00f)); // #D4AF37 Egyptian Gold
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(255.f/255.f, 215.f/255.f, 0.f/255.f, 1.00f));   // #FFD700 Pure Gold
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);

    if (ImGui::Button("INICIAR JOGO", ImVec2(btnW, btnH)))
    {
        if (onPlayGame) onPlayGame();
    }
    if (m_focusMainMenu)
    {
        ImGui::SetKeyboardFocusHere(-1);
        m_focusMainMenu = false;
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    const float starRadius = std::max(4.0f, 6.0f * layoutScale);
    const float starGap = 8.0f * layoutScale;
    const float labelWidth = ImGui::CalcTextSize("INICIAR JOGO").x;
    DrawGoldStar(drawList,
                 ImVec2(centerX - labelWidth * 0.5f - starGap - starRadius,
                        btn2Y + btnH * 0.5f), starRadius);
    DrawGoldStar(drawList,
                 ImVec2(centerX + labelWidth * 0.5f + starGap + starRadius,
                        btn2Y + btnH * 0.5f), starRadius);

    // --- BUTTON 3: MODS ---
    const float btn3Y = btn2Y + btnH + buttonGap;
    ImGui::SetCursorPos(ImVec2(btnX, btn3Y));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(14.f/255.f, 10.f/255.f, 26.f/255.f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(42.f/255.f, 31.f/255.f, 66.f/255.f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(161.f/255.f, 161.f/255.f, 171.f/255.f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);

    if (ImGui::Button("MODS", ImVec2(btnW, btnH)))
    {
        m_showModsModal = true;
        ImGui::OpenPopup("MODS");
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // --- BUTTON 4: CONFIGURACOES ---
    const float btn4Y = btn3Y + btnH + buttonGap;
    ImGui::SetCursorPos(ImVec2(btnX, btn4Y));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(14.f/255.f, 10.f/255.f, 26.f/255.f, 0.70f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(42.f/255.f, 31.f/255.f, 66.f/255.f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(161.f/255.f, 161.f/255.f, 171.f/255.f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);

    if (ImGui::Button("CONFIGURACOES", ImVec2(btnW, btnH)))
    {
        m_showSettingsModal = true;
        ImGui::OpenPopup("CONFIGURACOES");
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);

    // 6. Status Line Footer
    const float statusY = btn4Y + btnH + statusGap;
    if (isRomValid)
    {
        const char* status = "ROM PRONTA - SLUS-01411 - DX12 - 60 FPS";
        ImVec2 sz = ImGui::CalcTextSize(status);
        drawList->AddText(ImVec2(centerX - sz.x * 0.5f, statusY), IM_COL32(74, 232, 120, 255), status);
    }
    else
    {
        const char* status = "Selecione uma imagem de disco (.BIN) para iniciar";
        ImVec2 sz = ImGui::CalcTextSize(status);
        drawList->AddText(ImVec2(centerX - sz.x * 0.5f, statusY), IM_COL32(212, 175, 55, 230), status);
    }

    // Modal: MODS
    if (m_showModsModal || ImGui::IsPopupOpen("MODS"))
    {
        ImGui::SetNextWindowSize(ImVec2(480, 360), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(centerX - 240, screenH * 0.5f - 180), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.05f, 0.15f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.83f, 0.69f, 0.22f, 1.00f)); // Gold border
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);

        if (ImGui::BeginPopupModal("MODS", &m_showModsModal, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Ajustes de Gameplay & Regras:");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox("Drop de Todas as 722 Cartas (Correção de Drop)", &m_settings.dropMissingCards);
            ImGui::SetItemDefaultFocus();
            ImGui::Spacing();
            ImGui::Checkbox("IA Aprimorada (Modo Desafio / Hard Mode)", &m_settings.aiHardMode);
            ImGui::Spacing();
            ImGui::Checkbox("Monstros 3D Animados nos Duelos", &m_settings.monsters3D);
            ImGui::Spacing();
            ImGui::Checkbox("Textos e Texturas em Alta Definição (HD)", &m_settings.hdText);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const bool cancelMods = ImGui::IsWindowFocused() &&
                (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape));
            if (ImGui::Button("Fechar", ImVec2(120, 36)) || cancelMods)
            {
                m_showModsModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    // Modal: CONFIGURAÇÕES
    if (m_showSettingsModal || ImGui::IsPopupOpen("CONFIGURACOES"))
    {
        ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(centerX - 260, screenH * 0.5f - 210), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.08f, 0.05f, 0.15f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.83f, 0.69f, 0.22f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);

        if (ImGui::BeginPopupModal("CONFIGURACOES", &m_showSettingsModal, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Gráficos & Renderização DirectX 12:");
            ImGui::Separator();
            ImGui::Spacing();

            const char* resOptions[] = { "1x Nativo (240p Original)", "2x Full HD (1080p)", "4x Ultra HD (4K)" };
            int currentResIdx = (m_settings.resolutionScale == 1) ? 0 : (m_settings.resolutionScale == 2) ? 1 : 2;
            if (ImGui::Combo("Resolução Interna", &currentResIdx, resOptions, IM_ARRAYSIZE(resOptions)))
            {
                m_settings.resolutionScale = (currentResIdx == 0) ? 1 : (currentResIdx == 1) ? 2 : 4;
            }
            ImGui::SetItemDefaultFocus();
            ImGui::Spacing();

            const char* aspectOptions[] = { "4:3 Clássico (Pillarbox)", "16:9 Widescreen Nativo" };
            int currentAspect = m_settings.widescreen ? 1 : 0;
            if (ImGui::Combo("Proporção da Tela", &currentAspect, aspectOptions, IM_ARRAYSIZE(aspectOptions)))
            {
                m_settings.widescreen = (currentAspect == 1);
            }
            ImGui::Spacing();

            const char* filterOptions[] = { "Linear (Suave)", "Point (Pixels Nítidos Retrô)" };
            int currentFilter = m_settings.bilinearFilter ? 0 : 1;
            if (ImGui::Combo("Filtro de Textura", &currentFilter, filterOptions, IM_ARRAYSIZE(filterOptions)))
            {
                m_settings.bilinearFilter = (currentFilter == 0);
            }

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.0f, 1.0f), "Áudio XAudio2:");
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SliderFloat("Volume Geral", &m_settings.masterVolume, 0.0f, 1.0f, "%.0f%%");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const bool cancelSettings = ImGui::IsWindowFocused() &&
                (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape));
            if (ImGui::Button("Salvar & Fechar", ImVec2(160, 36)) || cancelSettings)
            {
                m_showSettingsModal = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void ImGuiFrontend::DrawInGameOverlay(bool* pOpen,
                                      const std::function<void()>& onResume,
                                      const std::function<void()>& onQuickSave,
                                      const std::function<void()>& onQuickLoad,
                                      const std::function<void()>& onReset,
                                      const std::function<void()>& onQuit)
{
    if (!pOpen || !*pOpen) return;

    ImGuiIO& io = ImGui::GetIO();
    float screenW = io.DisplaySize.x;
    float screenH = io.DisplaySize.y;
    float centerX = screenW * 0.5f;

    // Semi-transparent Egyptian dim backdrop
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->AddRectFilled(ImVec2(0, 0), ImVec2(screenW, screenH), IM_COL32(5, 3, 10, 190));

    float menuW = 380.0f;
    float menuH = 430.0f;
    ImGui::SetNextWindowSize(ImVec2(menuW, menuH), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(centerX - menuW * 0.5f, screenH * 0.5f - menuH * 0.5f), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.05f, 0.14f, 0.96f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.83f, 0.69f, 0.22f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    if (m_focusInGameMenu) ImGui::SetNextWindowFocus();
    if (ImGui::Begin("MENU DE PAUSA • EGÍPCIO", pOpen, flags))
    {
        ImGui::Spacing();
        DrawEgyptianDivider(menuW * 0.5f, ImGui::GetCursorPosY() + 10.0f, menuW - 60.0f);
        ImGui::Dummy(ImVec2(0, 24));

        float btnW = menuW - 48.0f;
        float btnH = 44.0f;

        if (ImGui::Button("CONTINUAR DUELO", ImVec2(btnW, btnH)))
        {
            *pOpen = false;
            if (onResume) onResume();
        }
        if (m_focusInGameMenu)
        {
            ImGui::SetKeyboardFocusHere(-1);
            m_focusInGameMenu = false;
        }
        ImGui::Spacing();

        if (ImGui::Button("SALVAR RÁPIDO (SLOT 1)", ImVec2(btnW, btnH)))
        {
            if (onQuickSave) onQuickSave();
        }
        ImGui::Spacing();

        if (ImGui::Button("CARREGAR RÁPIDO (SLOT 1)", ImVec2(btnW, btnH)))
        {
            if (onQuickLoad) onQuickLoad();
            *pOpen = false;
        }
        ImGui::Spacing();

        if (ImGui::Button("REINICIAR DUELO", ImVec2(btnW, btnH)))
        {
            if (onReset) onReset();
            *pOpen = false;
        }
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.10f, 0.12f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.70f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("SAIR PARA O MENU PRINCIPAL", ImVec2(btnW, btnH)))
        {
            *pOpen = false;
            if (onQuit) onQuit();
        }
        ImGui::PopStyleColor(2);

    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
