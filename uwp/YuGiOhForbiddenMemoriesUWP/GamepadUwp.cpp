#include "GamepadUwp.h"
#include <windows.gaming.input.h>
#include <wrl/client.h>
#include <atomic>

using namespace Windows::Gaming::Input;

static std::atomic<uint16_t> s_padBits[2] = { 0, 0 };
static std::atomic<bool> s_connected[2] = { false, false };
static std::atomic<bool> s_pauseRequested{ false };

void GamepadUwp::Initialize()
{
}

void GamepadUwp::Poll()
{
    auto gamepads = Gamepad::Gamepads;
    unsigned int count = gamepads->Size;

    for (int port = 0; port < 2; port++)
    {
        if (port >= (int)count)
        {
            s_connected[port] = false;
            s_padBits[port] = 0;
            continue;
        }

        s_connected[port] = true;
        auto gamepad = gamepads->GetAt(port);
        auto reading = gamepad->GetCurrentReading();

        uint16_t bits = 0;

        // D-Pad
        if ((reading.Buttons & GamepadButtons::DPadUp) != GamepadButtons::None)    bits |= 0x0010u;
        if ((reading.Buttons & GamepadButtons::DPadRight) != GamepadButtons::None) bits |= 0x0020u;
        if ((reading.Buttons & GamepadButtons::DPadDown) != GamepadButtons::None)  bits |= 0x0040u;
        if ((reading.Buttons & GamepadButtons::DPadLeft) != GamepadButtons::None)  bits |= 0x0080u;

        // Left Thumbstick mapping to D-Pad with 0.35 deadzone
        if (reading.LeftThumbstickY > 0.35)  bits |= 0x0010u;
        if (reading.LeftThumbstickX > 0.35)  bits |= 0x0020u;
        if (reading.LeftThumbstickY < -0.35) bits |= 0x0040u;
        if (reading.LeftThumbstickX < -0.35) bits |= 0x0080u;

        // Face Buttons (A = Cross, B = Circle, X = Square, Y = Triangle)
        if ((reading.Buttons & GamepadButtons::A) != GamepadButtons::None) bits |= 0x4000u;
        if ((reading.Buttons & GamepadButtons::B) != GamepadButtons::None) bits |= 0x2000u;
        if ((reading.Buttons & GamepadButtons::X) != GamepadButtons::None) bits |= 0x8000u;
        if ((reading.Buttons & GamepadButtons::Y) != GamepadButtons::None) bits |= 0x1000u;

        // Bumpers & Triggers
        if ((reading.Buttons & GamepadButtons::LeftShoulder) != GamepadButtons::None)  bits |= 0x0400u; // L1
        if ((reading.Buttons & GamepadButtons::RightShoulder) != GamepadButtons::None) bits |= 0x0800u; // R1
        if (reading.LeftTrigger > 0.3)  bits |= 0x0100u; // L2
        if (reading.RightTrigger > 0.3) bits |= 0x0200u; // R2

        // Thumbstick clicks
        if ((reading.Buttons & GamepadButtons::LeftThumbstick) != GamepadButtons::None)  bits |= 0x0002u; // L3
        if ((reading.Buttons & GamepadButtons::RightThumbstick) != GamepadButtons::None) bits |= 0x0004u; // R3

        // Menu / View
        if ((reading.Buttons & GamepadButtons::Menu) != GamepadButtons::None) bits |= 0x0008u; // Start
        if ((reading.Buttons & GamepadButtons::View) != GamepadButtons::None) bits |= 0x0001u; // Select

        // In-game pause toggle detection (e.g. pressing View and Menu together, or View for > 1 sec)
        if (port == 0 && (reading.Buttons & (GamepadButtons::Menu | GamepadButtons::View)) == (GamepadButtons::Menu | GamepadButtons::View))
        {
            s_pauseRequested = true;
        }

        s_padBits[port] = bits;
    }
}

uint16_t GamepadUwp::GetPadBits(int port)
{
    if (port < 0 || port > 1) return 0;
    return s_padBits[port];
}

bool GamepadUwp::IsConnected(int port)
{
    if (port < 0 || port > 1) return false;
    return s_connected[port];
}

bool GamepadUwp::IsPauseRequested()
{
    return s_pauseRequested;
}

void GamepadUwp::ClearPauseRequest()
{
    s_pauseRequested = false;
}
