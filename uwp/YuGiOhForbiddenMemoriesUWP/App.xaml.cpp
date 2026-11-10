#include "pch.h"
#include "App.xaml.h"
#include "MainPage.xaml.h"
#include <windows.h>
#include <fstream>
#include <string>

using namespace YuGiOhForbiddenMemoriesUWP;
using namespace Platform;
using namespace Windows::ApplicationModel;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Interop;

static void LogBoot(const std::wstring& msg)
{
    OutputDebugStringW((L"[UWP_BOOT] " + msg + L"\n").c_str());
    try
    {
        auto folder = Windows::Storage::ApplicationData::Current->LocalFolder;
        if (folder != nullptr)
        {
            std::wstring logPath = folder->Path->Data() + std::wstring(L"\\boot_log.txt");
            FILE* f = nullptr;
            if (_wfopen_s(&f, logPath.c_str(), L"a, ccs=UTF-8") == 0 && f)
            {
                fwprintf(f, L"[BOOT] %s\n", msg.c_str());
                fflush(f);
                fclose(f);
            }
        }
    }
    catch (...) {}
}

App::App()
{
    LogBoot(L"App::App() constructor entered");
    InitializeComponent();
    // Xbox defaults to a virtual mouse even for native XAML applications.
    // Navigation is supplied by Windows.Gaming.Input and Dear ImGui instead.
    RequiresPointerMode = ApplicationRequiresPointerMode::WhenRequested;
    Suspending += ref new SuspendingEventHandler(this, &App::OnSuspending);
    LogBoot(L"App::App() constructor finished");
}

void App::OnLaunched(LaunchActivatedEventArgs^ e)
{
    LogBoot(L"App::OnLaunched() entered");
    try
    {
        auto rootFrame = dynamic_cast<Frame^>(Window::Current->Content);

        if (rootFrame == nullptr)
        {
            rootFrame = ref new Frame();
            rootFrame->NavigationFailed += ref new Windows::UI::Xaml::Navigation::NavigationFailedEventHandler(this, &App::OnNavigationFailed);

            if (e->PreviousExecutionState == ApplicationExecutionState::Terminated)
            {
            }

            Window::Current->Content = rootFrame;
            LogBoot(L"Frame created and assigned to Window::Current->Content");
        }

        if (e->PrelaunchActivated == false)
        {
            if (rootFrame->Content == nullptr)
            {
                LogBoot(L"Navigating rootFrame to MainPage");
                rootFrame->Navigate(TypeName(MainPage::typeid), e->Arguments);
                LogBoot(L"Navigated to MainPage successfully");
            }
            Window::Current->Activate();
            LogBoot(L"Window::Current->Activate() called");
        }
    }
    catch (Platform::Exception^ ex)
    {
        LogBoot(std::wstring(L"App::OnLaunched() FAILED: ") + ex->Message->Data());
    }
    catch (...)
    {
        LogBoot(L"App::OnLaunched() unknown exception");
    }
}

void App::OnSuspending(Object^ sender, SuspendingEventArgs^ e)
{
    (void) sender;
    (void) e;
}

void App::OnNavigationFailed(Platform::Object^ sender, Windows::UI::Xaml::Navigation::NavigationFailedEventArgs^ e)
{
    LogBoot(std::wstring(L"OnNavigationFailed: ") + e->SourcePageType.Name->Data());
    throw ref new FailureException("Failed to load Page " + e->SourcePageType.Name);
}
