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

        void OnCompositionRendering(Platform::Object^ sender, Platform::Object^ e);
        void CheckAvailableRoms();
        void StartGameWithCurrentRom();

        std::unique_ptr<D3D12Renderer> m_d3d12Renderer;
        std::wstring m_currentRomPath;
        bool m_isRomValid = false;
        bool m_inGame = false;
        bool m_rendererInitialized = false;

        Windows::Foundation::EventRegistrationToken m_renderingToken;
    };
}
