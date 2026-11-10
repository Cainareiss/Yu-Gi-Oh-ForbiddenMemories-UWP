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
        m_isXbox = Windows::System::Profile::AnalyticsInfo::VersionInfo->DeviceFamily == L"Windows.Xbox";
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
        // Xbox exposes the CoreWindow back action as navigation.  Consume it
        // in the app so B never terminates the UWP page like a browser tab.
        if (m_isXbox) coreWindow->PointerCursor = nullptr;
        coreWindow->KeyDown += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &MainPage::OnKeyDown);
        coreWindow->KeyUp += ref new TypedEventHandler<CoreWindow^, KeyEventArgs^>(this, &MainPage::OnKeyUp);
        coreWindow->CharacterReceived += ref new TypedEventHandler<CoreWindow^, CharacterReceivedEventArgs^>(this, &MainPage::OnCharacterReceived);
    }

    auto navigation = SystemNavigationManager::GetForCurrentView();
    if (navigation != nullptr)
    {
        navigation->BackRequested += ref new EventHandler<BackRequestedEventArgs^>(
            this, &MainPage::OnBackRequested);
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
    Focus(Windows::UI::Xaml::FocusState::Programmatic);
    CheckAvailableRoms();
    StartMenuMusic();
}

void MainPage::CheckAvailableRoms()
{
    const uint64_t request = ++m_romRequestGeneration;
    auto finish = [this, request](concurrency::task<StorageFile^> completed)
    {
        StorageFile^ file = nullptr;
        try { file = completed.get(); }
        catch (Platform::Exception^) {}
        catch (const std::exception&) {}

        Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
            ref new DispatchedHandler([this, request, file]()
        {
            // A picker selection made while restoration was pending wins.
            if (request != m_romRequestGeneration) return;
            if (file != nullptr)
            {
                // The FutureAccessList token supplies permission as well as
                // the file. Apply the restored handle and the visible path
                // together after checking that no newer selection won.
                StorageUwp::SetActiveDiscFile(file);
                const auto info = StorageUwp::VerifyDisc(file->Path->Data());
                m_currentRomPath = info.path;
                m_isRomValid = info.isValid;
                if (m_isRomValid) return;
            }

            const auto available = StorageUwp::ScanAvailableRoms();
            if (!available.empty())
            {
                m_currentRomPath = available[0].path;
                m_isRomValid = available[0].isValid;
            }
        }));
    };

    auto list = StorageApplicationPermissions::FutureAccessList;
    if (list->ContainsItem(L"SelectedRom"))
    {
        concurrency::create_task(list->GetFileAsync(L"SelectedRom")).then(finish);
        return;
    }

    const auto cachedPath = StorageUwp::GetActiveDiscPath();
    if (!cachedPath.empty())
    {
        LogPage(L"Checking cached ROM path: " + cachedPath);
        concurrency::create_task(StorageFile::GetFileFromPathAsync(
            ref new Platform::String(cachedPath.c_str()))).then(finish);
        return;
    }

    const auto available = StorageUwp::ScanAvailableRoms();
    if (!available.empty())
    {
        m_currentRomPath = available[0].path;
        m_isRomValid = available[0].isValid;
    }
}

void MainPage::OnPointerPressed(Object^ sender, PointerRoutedEventArgs^ e)
{
    if (m_isXbox) { e->Handled = true; return; }
    auto pt = e->GetCurrentPoint(m_swapChainPanel);
    int button = 0; // 0 = Left, 1 = Right, 2 = Middle
    if (pt->Properties->IsRightButtonPressed) button = 1;
    if (pt->Properties->IsMiddleButtonPressed) button = 2;

    ImGuiFrontend::Instance().OnPointerPressed(pt->Position.X, pt->Position.Y, button);
    e->Handled = true;
}

void MainPage::OnPointerReleased(Object^ sender, PointerRoutedEventArgs^ e)
{
    if (m_isXbox) { e->Handled = true; return; }
    auto pt = e->GetCurrentPoint(m_swapChainPanel);
    int button = 0;
    if (pt->Properties->PointerUpdateKind == Windows::UI::Input::PointerUpdateKind::RightButtonReleased) button = 1;
    if (pt->Properties->PointerUpdateKind == Windows::UI::Input::PointerUpdateKind::MiddleButtonReleased) button = 2;

    ImGuiFrontend::Instance().OnPointerReleased(pt->Position.X, pt->Position.Y, button);
    e->Handled = true;
}

void MainPage::OnPointerMoved(Object^ sender, PointerRoutedEventArgs^ e)
{
    if (m_isXbox) { e->Handled = true; return; }
    auto pt = e->GetCurrentPoint(m_swapChainPanel);
    ImGuiFrontend::Instance().OnPointerMoved(pt->Position.X, pt->Position.Y);
    e->Handled = true;
}

void MainPage::OnPointerWheelChanged(Object^ sender, PointerRoutedEventArgs^ e)
{
    if (m_isXbox) { e->Handled = true; return; }
    auto pt = e->GetCurrentPoint(m_swapChainPanel);
    float delta = static_cast<float>(pt->Properties->MouseWheelDelta) / 120.0f;
    ImGuiFrontend::Instance().OnPointerWheel(delta);
    e->Handled = true;
}

void MainPage::OnKeyDown(CoreWindow^ sender, KeyEventArgs^ e)
{
    int vk = static_cast<int>(e->VirtualKey);
    // Gamepad input is polled once per frame. Do not let XAML also navigate
    // or synthesize clicks from the same controller key.
    if (vk >= static_cast<int>(Windows::System::VirtualKey::GamepadA) &&
        vk <= static_cast<int>(Windows::System::VirtualKey::GamepadRightThumbstickLeft))
    {
        e->Handled = true;
        return;
    }
    if (vk == 0x0D) m_keyboardLaunchKeys |= 1;
    if (vk == 0x20) m_keyboardLaunchKeys |= 2;
    GamepadUwp::SetKeyboardKey(vk, true);
    ImGuiFrontend::Instance().OnKeyDown(vk);
    e->Handled = true;
}

void MainPage::OnKeyUp(CoreWindow^ sender, KeyEventArgs^ e)
{
    int vk = static_cast<int>(e->VirtualKey);
    if (vk >= static_cast<int>(Windows::System::VirtualKey::GamepadA) &&
        vk <= static_cast<int>(Windows::System::VirtualKey::GamepadRightThumbstickLeft))
    {
        e->Handled = true;
        return;
    }
    if (vk == 0x0D) m_keyboardLaunchKeys &= static_cast<uint8_t>(~1u);
    if (vk == 0x20) m_keyboardLaunchKeys &= static_cast<uint8_t>(~2u);
    GamepadUwp::SetKeyboardKey(vk, false);
    ImGuiFrontend::Instance().OnKeyUp(vk);
    e->Handled = true;
}

void MainPage::OnCharacterReceived(CoreWindow^ sender, CharacterReceivedEventArgs^ e)
{
    wchar_t ch = static_cast<wchar_t>(e->KeyCode);
    ImGuiFrontend::Instance().OnCharacter(ch);
}

void MainPage::OnBackRequested(Object^ sender, BackRequestedEventArgs^ e)
{
    (void)sender;
    if (e == nullptr) return;

    // B belongs to the PS1 game and ImGui's cancel action. Consume the shell
    // navigation request without opening or toggling the in-game overlay.
    e->Handled = true;
}

void MainPage::OnMenuMusicOpened(Windows::Media::Playback::MediaPlayer^ sender, Object^ e)
{
    (void)e;
    LogPage(L"Menu music opened; duration=" + std::to_wstring(sender->PlaybackSession->NaturalDuration.Duration / 10000000) + L" seconds.");
    Dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([this]()
    {
        // Opening is asynchronous. A late completion must not restart the
        // theme after the player has pressed Play or entered the game.
        if (m_menuMusicRequested && !m_inGame && !m_startingGame)
            m_menuPlayer->Play();
    }));
}

void MainPage::OnMenuMusicFailed(Windows::Media::Playback::MediaPlayer^ sender, Windows::Media::Playback::MediaPlayerFailedEventArgs^ e)
{
    (void)sender;
    std::wstring message = L"Menu music failed: ";
    if (e != nullptr && e->ErrorMessage != nullptr)
    {
        message += e->ErrorMessage->Data();
    }
    LogPage(message);
}

void MainPage::OnMenuPlaybackStateChanged(Windows::Media::Playback::MediaPlaybackSession^ sender, Object^ e)
{
    (void)e;
    if (sender->PlaybackState == Windows::Media::Playback::MediaPlaybackState::Playing)
        LogPage(L"Menu music playback state: Playing.");
    else if (sender->PlaybackState == Windows::Media::Playback::MediaPlaybackState::Paused)
        LogPage(L"Menu music playback state: Paused.");
}

void MainPage::QueueStartGame()
{
    if (!m_launchInputArmed)
    {
        LogPage(L"Start request ignored until the launch button is released.");
        return;
    }
    if (m_startingGame || m_inGame || GameEngineBridge::Instance().IsRunning())
    {
        LogPage(L"Duplicate start request ignored.");
        return;
    }

    // Reserve the launch before any storage operation can dispatch events.
    // Run it after RenderMenu has finished recording/presenting its frame.
    m_launchInputArmed = false;
    m_startingGame = true;
    try
    {
        Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
            ref new DispatchedHandler([this]()
        {
            ImGuiFrontend::Instance().ResetInput();
            try
            {
                StartGameWithCurrentRom();
            }
            catch (Platform::Exception^ ex)
            {
                LogPage(std::wstring(L"Game launch failed: ") + ex->Message->Data());
                m_inGame = GameEngineBridge::Instance().IsRunning();
                if (!m_inGame) StartMenuMusic();
            }
            catch (const std::exception& ex)
            {
                GameEngineBridge::Instance().SetLastError(ex.what());
                LogPage(L"Game launch failed with a native exception.");
                m_inGame = GameEngineBridge::Instance().IsRunning();
                if (!m_inGame) StartMenuMusic();
            }
            if (!m_inGame) ImGuiFrontend::Instance().ResetInput();
            m_startingGame = false;
        }));
    }
    catch (...)
    {
        m_startingGame = false;
        throw;
    }
}

void MainPage::StartGameWithCurrentRom()
{
    if (m_inGame || GameEngineBridge::Instance().IsRunning())
    {
        LogPage(L"Duplicate game launch ignored.");
        return;
    }
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
    settings.masterVolume = s.masterVolume;

    StopMenuMusic();
    m_inGame = GameEngineBridge::Instance().StartGame(m_d3d12Renderer.get(), m_currentRomPath, settings);
    if (!m_inGame)
    {
        StartMenuMusic();
    }
}

void MainPage::StartMenuMusic()
{
    m_menuMusicRequested = true;
    try
    {
        using namespace Windows::Media::Playback;
        if (m_menuPlayer == nullptr)
        {
            // Audio playback has no dependency on the visibility/loading
            // of a XAML element or on the DirectX presentation surface.
            m_menuPlayer = ref new MediaPlayer();
            m_menuPlayer->AutoPlay = false;
            m_menuPlayer->AudioCategory = MediaPlayerAudioCategory::GameMedia;
            m_menuPlayer->CommandManager->IsEnabled = false;
            m_menuPlayer->IsLoopingEnabled = true;
            m_menuPlayer->IsMuted = false;
            m_menuPlayer->Volume = 0.70;
            m_menuPlayer->MediaOpened += ref new TypedEventHandler<MediaPlayer^, Object^>(this, &MainPage::OnMenuMusicOpened);
            m_menuPlayer->MediaFailed += ref new TypedEventHandler<MediaPlayer^, MediaPlayerFailedEventArgs^>(this, &MainPage::OnMenuMusicFailed);
            m_menuPlayer->PlaybackSession->PlaybackStateChanged += ref new TypedEventHandler<MediaPlaybackSession^, Object^>(this, &MainPage::OnMenuPlaybackStateChanged);
            m_menuPlayer->Source = Windows::Media::Core::MediaSource::CreateFromUri(
                ref new Uri(L"ms-appx:///Assets/YuGiOhMenuTheme.mp3"));
        }
        m_menuPlayer->Play();
        LogPage(L"Menu theme requested: native audio player, loop enabled, volume 70%.");
    }
    catch (Platform::Exception^ ex)
    {
        LogPage(std::wstring(L"Menu theme initialization failed: ") + ex->Message->Data());
    }
}

void MainPage::StopMenuMusic()
{
    m_menuMusicRequested = false;
    m_menuMusicLastPosition = 0;
    if (m_menuPlayer != nullptr)
    {
        m_menuPlayer->Pause();
        m_menuPlayer->PlaybackSession->Position = TimeSpan{};
        LogPage(L"Menu theme stopped before starting the game.");
    }
}

void MainPage::OnCompositionRendering(Object^ sender, Object^ e)
{
    // Storage/WinRT can dispatch nested UI events. A nested callback must
    // never redraw the menu or invoke Play on the current command list.
    if (m_isRendering || m_startingGame) return;
    struct RenderingScope
    {
        bool& active;
        RenderingScope(bool& flag) : active(flag) { active = true; }
        ~RenderingScope() { active = false; }
    } renderingScope(m_isRendering);

    // Record progress and natural wrap-around to distinguish a loaded file
    // from a theme that is actually playing and looping.
    if (!m_inGame && m_menuMusicRequested && m_menuPlayer != nullptr && ++m_menuMusicPollFrames % 60 == 0 &&
        m_menuPlayer->PlaybackSession->PlaybackState == Windows::Media::Playback::MediaPlaybackState::Playing)
    {
        const auto position = m_menuPlayer->PlaybackSession->Position.Duration;
        if (position < m_menuMusicLastPosition)
            LogPage(L"Menu theme loop restarted.");
        else if (m_menuMusicLastPosition == 0 && position > 0)
            LogPage(L"Menu theme playback position is advancing.");
        m_menuMusicLastPosition = position;
    }

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
    else
    {
        // SwapChainPanel starts at its XAML measure size and receives the
        // real window size a few composition ticks later. Keep the renderer
        // and ImGui coordinate space synchronized with that change.
        uint32_t width = static_cast<uint32_t>(m_swapChainPanel->ActualWidth);
        uint32_t height = static_cast<uint32_t>(m_swapChainPanel->ActualHeight);
        if (width != 0 && height != 0 &&
            (width != m_d3d12Renderer->GetWidth() || height != m_d3d12Renderer->GetHeight()))
        {
            m_d3d12Renderer->Resize(width, height);
        }
    }

    // 2. Poll Gamepad input and update ImGui
    GamepadUwp::Poll();
    const uint16_t padBits = GamepadUwp::GetControllerBits(0);
    // Enter/Space arrive through ImGui's keyboard path. Only a physical
    // controller enters the gamepad path; this avoids duplicate activation.
    if ((padBits & 0x0040u) == 0 && m_keyboardLaunchKeys == 0)
        m_launchInputArmed = true;
    if (m_isXbox) Window::Current->CoreWindow->PointerCursor = nullptr;
    ImGuiFrontend::Instance().UpdateGamepad(padBits, GamepadUwp::IsConnected(0), m_inGame);
    GamepadUwp::SetUiCapture(!m_inGame || ImGuiFrontend::Instance().IsInGameMenuOpen());

    // Keep changes made in the R3 settings/modals synchronized with the
    // worker-thread core. GameEngineBridge queues them at the next frame.
    if (m_inGame)
    {
        const auto& frontend = ImGuiFrontend::Instance().GetSettings();
        GameSettings liveSettings;
        liveSettings.resolutionScale = frontend.resolutionScale;
        liveSettings.widescreen = frontend.widescreen;
        liveSettings.bilinearFilter = frontend.bilinearFilter;
        liveSettings.hdText = frontend.hdText;
        liveSettings.monsters3D = frontend.monsters3D;
        liveSettings.aiHardMode = frontend.aiHardMode;
        liveSettings.dropMissingCards = frontend.dropMissingCards;
        liveSettings.masterVolume = frontend.masterVolume;
        liveSettings.activeSaveSlot = GameEngineBridge::Instance().GetSettings().activeSaveSlot;
        GameEngineBridge::Instance().SetSettings(liveSettings);
    }

    // 3. Render state handling
    if (!m_inGame)
    {
        // Render Egyptian ImGui Main Menu (matches E:\Sem tÃ­tulo.svg)
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
                        Dispatcher->RunAsync(CoreDispatcherPriority::Normal,
                            ref new DispatchedHandler([this, file]()
                        {
                            ++m_romRequestGeneration;
                            StorageUwp::SetActiveDiscFile(file);
                            const std::wstring path = file->Path->Data();
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
                QueueStartGame();
            }
        );
    }
    else
    {
        GameEngineBridge::Instance().PresentPendingFrame();
        // In-game: if game has exited, return to menu
        if (!GameEngineBridge::Instance().IsRunning())
        {
            GameEngineBridge::Instance().StopGame();
            m_inGame = false;
            m_launchInputArmed = false;
            ImGuiFrontend::Instance().ResetInput();
            StartMenuMusic();
            LogPage(L"Game engine stopped. Returning to Egyptian Main Menu.");
        }
    }
}


