#define _CRT_SECURE_NO_WARNINGS
#include "pch.h"
#include "MainPage.xaml.h"
#include "GamepadUwp.h"
#include "StorageUwp.h"
#include "ImGuiFrontend.h"
#include <windows.h>
#include <ppltasks.h>

using namespace YuGiOhForbiddenMemoriesUWP;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Storage;
using namespace Windows::Storage::Pickers;
using namespace Windows::Storage::AccessCache;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Input;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Navigation;

static void LogPage(const std::wstring& msg)
{
    OutputDebugStringW((L"[UWP_PAGE] " + msg + L"\n").c_str());
    try
    {
        auto folder = Windows::Storage::ApplicationData::Current->LocalFolder;
        if (folder != nullptr)
        {
            std::wstring logPath = folder->Path->Data() + std::wstring(L"\\boot_log.txt");
            FILE* f = nullptr;
            if (_wfopen_s(&f, logPath.c_str(), L"a, ccs=UTF-8") == 0 && f)
            {
                fwprintf(f, L"[PAGE] %s\n", msg.c_str());
                fflush(f);
                fclose(f);
            }
        }
    }
    catch (...) {}
}

MainPage::MainPage()
{
    LogPage(L"MainPage::MainPage() started");

    try
    {
        InitializeComponent();
        LogPage(L"MainPage::InitializeComponent() succeeded");
    }
    catch (Platform::Exception^ ex)
    {
        LogPage(std::wstring(L"MainPage::InitializeComponent() FAILED: ") + ex->Message->Data());
    }

    try
    {
        StorageUwp::Initialize();
        LogPage(L"StorageUwp::Initialize() succeeded");
    }
    catch (...)
    {
        LogPage(L"StorageUwp::Initialize() error");
    }

    try
    {
        GamepadUwp::Initialize();
        LogPage(L"GamepadUwp::Initialize() succeeded");
    }
    catch (...)
    {
        LogPage(L"GamepadUwp::Initialize() error");
    }

    try
    {
        m_d3d12Renderer = std::make_unique<D3D12Renderer>();
        LogPage(L"m_d3d12Renderer created successfully");
    }
    catch (...)
    {
        LogPage(L"m_d3d12Renderer constructor error");
    }

    // Register keyboard events from CoreWindow
    auto coreWindow = Window::Current->CoreWindow;
    if (coreWindow != nullptr)
    {
        coreWindow->KeyDown += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &MainPage::OnKeyDown);
        coreWindow->KeyUp += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &MainPage::OnKeyUp);
        coreWindow->CharacterReceived += ref new TypedEventHandler<CoreWindow^, CharacterReceivedEventArgs^>(this, &MainPage::OnCharacterReceived);
    }

    // Register CompositionTarget::Rendering for 60 FPS presentation loop
    try
    {
        m_renderingToken = CompositionTarget::Rendering::add(
            ref new EventHandler<Object^>(this, &MainPage::OnCompositionRendering));
        LogPage(L"CompositionTarget::Rendering handler registered");
    }
    catch (...)
    {
        LogPage(L"CompositionTarget::Rendering registration error");
    }

    LogPage(L"MainPage::MainPage() completed successfully");
}

void MainPage::OnNavigatedTo(NavigationEventArgs^ e)
{
    LogPage(L"MainPage::OnNavigatedTo() started");
    Page::OnNavigatedTo(e);
    CheckAvailableRoms();
}

void MainPage::CheckAvailableRoms()
{
    std::wstring cachedPath = StorageUwp::GetActiveDiscPath();
    if (!cachedPath.empty())
    {
        LogPage(L"Checking cached ROM path: " + cachedPath);
        concurrency::create_task(StorageFile::GetFileFromPathAsync(ref new Platform::String(cachedPath.c_str())))
            .then([this, cachedPath](concurrency::task<StorageFile^> t)
        {
            try
            {
                StorageFile^ file = t.get();
                if (file != nullptr)
                {
                    StorageUwp::SetActiveDiscFile(file);
                    auto info = StorageUwp::VerifyDisc(cachedPath);
                    Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                        ref new DispatchedHandler([this, cachedPath, info]()
                    {
                        m_currentRomPath = cachedPath;
                        m_isRomValid = info.isValid;
                    }));
                    return;
                }
            }
            catch (...) {}

            auto available = StorageUwp::ScanAvailableRoms();
            if (!available.empty())
            {
                std::wstring nextPath = available[0].path;
                Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                    ref new DispatchedHandler([this, nextPath]()
                {
                    m_currentRomPath = nextPath;
                    auto info = StorageUwp::VerifyDisc(nextPath);
                    m_isRomValid = info.isValid;
                }));
            }
        });
        return;
    }

    auto available = StorageUwp::ScanAvailableRoms();
    if (!available.empty())
    {
        m_currentRomPath = available[0].path;
        auto info = StorageUwp::VerifyDisc(m_currentRomPath);
        m_isRomValid = info.isValid;
    }
}

void MainPage::OnPointerPressed(Object^ sender, PointerRoutedEventArgs^ e)
{
    auto pt = e->GetCurrentPoint(m_swapChainPanel);
    int button = 0; // 0 = Left, 1 = Right, 2 = Middle
    if (pt->Properties->IsRightButtonPressed) button = 1;
    if (pt->Properties->IsMiddleButtonPressed) button = 2;

    ImGuiFrontend::Instance().OnPointerPressed(pt->Position.X, pt->Position.Y, button);
    e->Handled = true;
}

void MainPage::OnPointerReleased(Object^ sender, PointerRoutedEventArgs^ e)
{
    auto pt = e->GetCurrentPoint(m_swapChainPanel);
    int button = 0;
    if (pt->Properties->PointerUpdateKind == Windows::UI::Input::PointerUpdateKind::RightButtonReleased) button = 1;
    if (pt->Properties->PointerUpdateKind == Windows::UI::Input::PointerUpdateKind::MiddleButtonReleased) button = 2;

    ImGuiFrontend::Instance().OnPointerReleased(pt->Position.X, pt->Position.Y, button);
    e->Handled = true;
}

void MainPage::OnPointerMoved(Object^ sender, PointerRoutedEventArgs^ e)
{
    auto pt = e->GetCurrentPoint(m_swapChainPanel);
    ImGuiFrontend::Instance().OnPointerMoved(pt->Position.X, pt->Position.Y);
    e->Handled = true;
}

void MainPage::OnPointerWheelChanged(Object^ sender, PointerRoutedEventArgs^ e)
{
    auto pt = e->GetCurrentPoint(m_swapChainPanel);
    float delta = static_cast<float>(pt->Properties->MouseWheelDelta) / 120.0f;
    ImGuiFrontend::Instance().OnPointerWheel(delta);
    e->Handled = true;
}

void MainPage::OnKeyDown(CoreWindow^ sender, KeyEventArgs^ e)
{
    int vk = static_cast<int>(e->VirtualKey);
    ImGuiFrontend::Instance().OnKeyDown(vk);
}

void MainPage::OnKeyUp(CoreWindow^ sender, KeyEventArgs^ e)
{
    int vk = static_cast<int>(e->VirtualKey);
    ImGuiFrontend::Instance().OnKeyUp(vk);
}

void MainPage::OnCharacterReceived(CoreWindow^ sender, CharacterReceivedEventArgs^ e)
{
    wchar_t ch = static_cast<wchar_t>(e->KeyCode);
    ImGuiFrontend::Instance().OnCharacter(ch);
}

void MainPage::StartGameWithCurrentRom()
{
    LogPage(L"StartGameWithCurrentRom called with: " + m_currentRomPath);
    if (m_currentRomPath.empty())
    {
        LogPage(L"ROM path is empty! Cannot start.");
        return;
    }

    auto info = StorageUwp::VerifyDisc(m_currentRomPath);
    if (!info.isValid)
    {
        LogPage(L"ROM is not valid! Cannot start.");
        return;
    }

    auto& s = ImGuiFrontend::Instance().GetSettings();
    GameSettings settings;
    settings.resolutionScale = s.resolutionScale;
    settings.widescreen = s.widescreen;
    settings.bilinearFilter = s.bilinearFilter;
    settings.hdText = s.hdText;
    settings.monsters3D = s.monsters3D;
    settings.aiHardMode = s.aiHardMode;
    settings.dropMissingCards = s.dropMissingCards;

    m_inGame = true;
    GameEngineBridge::Instance().StartGame(m_d3d12Renderer.get(), m_currentRomPath, settings);
}

void MainPage::OnCompositionRendering(Object^ sender, Object^ e)
{
    // 1. Ensure DirectX 12 renderer is initialized on the SwapChainPanel
    if (!m_rendererInitialized)
    {
        uint32_t width = static_cast<uint32_t>(m_swapChainPanel->ActualWidth);
        uint32_t height = static_cast<uint32_t>(m_swapChainPanel->ActualHeight);
        if (width == 0) width = 1920;
        if (height == 0) height = 1080;

        if (m_d3d12Renderer->Initialize(reinterpret_cast<IUnknown*>(m_swapChainPanel), width, height))
        {
            m_rendererInitialized = true;
            LogPage(L"D3D12Renderer initialized on SwapChainPanel with size " + std::to_wstring(width) + L"x" + std::to_wstring(height));
        }
        else
        {
            return;
        }
    }

    // 2. Poll Gamepad input and update ImGui
    GamepadUwp::Poll();
    uint16_t padBits = GamepadUwp::GetPadBits(0);
    ImGuiFrontend::Instance().UpdateGamepad(padBits);

    // 3. Render state handling
    if (!m_inGame)
    {
        // Render Egyptian ImGui Main Menu (matches E:\Sem título.svg)
        m_d3d12Renderer->RenderMenu(
            m_currentRomPath,
            m_isRomValid,
            [this]() {
                // SELECIONAR JOGO clicked
                auto picker = ref new FileOpenPicker();
                picker->ViewMode = PickerViewMode::List;
                picker->SuggestedStartLocation = PickerLocationId::ComputerFolder;
                picker->FileTypeFilter->Append(".bin");
                picker->FileTypeFilter->Append(".BIN");

                concurrency::create_task(picker->PickSingleFileAsync()).then([this](StorageFile^ file)
                {
                    if (file != nullptr)
                    {
                        StorageApplicationPermissions::FutureAccessList->AddOrReplace("SelectedRom", file);
                        StorageUwp::SetActiveDiscFile(file);
                        std::wstring path = file->Path->Data();
                        Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                            ref new DispatchedHandler([this, path]()
                        {
                            m_currentRomPath = path;
                            StorageUwp::SetActiveDiscPath(path);
                            auto info = StorageUwp::VerifyDisc(path);
                            m_isRomValid = info.isValid;
                        }));
                    }
                });
            },
            [this]() {
                // INICIAR JOGO clicked
                StartGameWithCurrentRom();
            }
        );
    }
    else
    {
        // In-game: if game has exited, return to menu
        if (!GameEngineBridge::Instance().IsRunning())
        {
            m_inGame = false;
            LogPage(L"Game engine stopped. Returning to Egyptian Main Menu.");
        }
    }
}
