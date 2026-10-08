#pragma once

#include "MainPage.g.h"
#include "D3D12Renderer.h"
#include "GameEngineBridge.h"
#include "ImGuiFrontend.h"
#include <memory>
#include <string>

namespace YuGiOhForbiddenMemoriesUWP
{
    public ref class MainPage sealed
    {
    public:
        MainPage();

        void OnPointerPressed(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e);
        void OnPointerReleased(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e);
        void OnPointerMoved(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e);
        void OnPointerWheelChanged(Platform::Object^ sender, Windows::UI::Xaml::Input::PointerRoutedEventArgs^ e);

    protected:
        virtual void OnNavigatedTo(Windows::UI::Xaml::Navigation::NavigationEventArgs^ e) override;

    private:
        void OnKeyDown(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ e);
        void OnKeyUp(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::KeyEventArgs^ e);
        void OnCharacterReceived(Windows::UI::Core::CoreWindow^ sender, Windows::UI::Core::CharacterReceivedEventArgs^ e);
        void OnBackRequested(Platform::Object^ sender, Windows::UI::Core::BackRequestedEventArgs^ e);
        void OnMenuMusicOpened(Windows::Media::Playback::MediaPlayer^ sender, Platform::Object^ e);
        void OnMenuMusicFailed(Windows::Media::Playback::MediaPlayer^ sender, Windows::Media::Playback::MediaPlayerFailedEventArgs^ e);
        void OnMenuPlaybackStateChanged(Windows::Media::Playback::MediaPlaybackSession^ sender, Platform::Object^ e);

        void OnCompositionRendering(Platform::Object^ sender, Platform::Object^ e);
        void CheckAvailableRoms();
        void QueueStartGame();
        void StartGameWithCurrentRom();
        void StartMenuMusic();
        void StopMenuMusic();

        std::unique_ptr<D3D12Renderer> m_d3d12Renderer;
        std::wstring m_currentRomPath;
        bool m_isRomValid = false;
        bool m_inGame = false;
        bool m_startingGame = false;
        bool m_isRendering = false;
        bool m_rendererInitialized = false;
        bool m_isXbox = false;
        Windows::Media::Playback::MediaPlayer^ m_menuPlayer = nullptr;
        bool m_menuMusicRequested = false;
        uint32_t m_menuMusicPollFrames = 0;
        int64_t m_menuMusicLastPosition = 0;

        Windows::Foundation::EventRegistrationToken m_renderingToken;
    };
}
