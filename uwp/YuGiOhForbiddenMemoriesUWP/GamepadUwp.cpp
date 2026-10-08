#include "GamepadUwp.h"
#include "UwpPadMapping.h"
#include <windows.gaming.input.h>
#include <wrl/client.h>
#include <atomic>

using namespace Windows::Gaming::Input;

static std::atomic<uint16_t> s_padBits[2] = { 0, 0 };
static std::atomic<uint16_t> s_keyboardBits{ 0 };
static std::atomic<uint16_t> s_keyboardPulse{ 0 };
static std::atomic<int> s_keyboardPulseFrames{ 0 };
static std::atomic<bool> s_connected[2] = { false, false };
static std::atomic<bool> s_uiCapture{ false };

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

        // The core consumes the original PS1 pad layout (src/game/input.h).
        // Keep the Xbox translation in that layout so the game and ImGui see
        // the same buttons.
        if ((reading.Buttons & GamepadButtons::DPadUp) != GamepadButtons::None)    bits |= 0x1000u;
        if ((reading.Buttons & GamepadButtons::DPadRight) != GamepadButtons::None) bits |= 0x2000u;
        if ((reading.Buttons & GamepadButtons::DPadDown) != GamepadButtons::None)  bits |= 0x4000u;
        if ((reading.Buttons & GamepadButtons::DPadLeft) != GamepadButtons::None)  bits |= 0x8000u;

        // Left Thumbstick mapping to D-Pad with 0.35 deadzone
        if (reading.LeftThumbstickY > 0.35)  bits |= 0x1000u;
        if (reading.LeftThumbstickX > 0.35)  bits |= 0x2000u;
        if (reading.LeftThumbstickY < -0.35) bits |= 0x4000u;
        if (reading.LeftThumbstickX < -0.35) bits |= 0x8000u;

        // Face Buttons (A = Cross, B = Circle, X = Square, Y = Triangle)
        if ((reading.Buttons & GamepadButtons::A) != GamepadButtons::None) bits |= 0x0040u;
        if ((reading.Buttons & GamepadButtons::B) != GamepadButtons::None) bits |= 0x0020u;
        if ((reading.Buttons & GamepadButtons::X) != GamepadButtons::None) bits |= 0x0080u;
        if ((reading.Buttons & GamepadButtons::Y) != GamepadButtons::None) bits |= 0x0010u;

        // Bumpers & Triggers
        if ((reading.Buttons & GamepadButtons::LeftShoulder) != GamepadButtons::None)  bits |= 0x0004u; // L1
        if ((reading.Buttons & GamepadButtons::RightShoulder) != GamepadButtons::None) bits |= 0x0008u; // R1
        if (reading.LeftTrigger > 0.3)  bits |= 0x0001u; // L2
        if (reading.RightTrigger > 0.3) bits |= 0x0002u; // R2

        // Thumbstick clicks
        // 0x0200/0x0400 are unused by the retail pad protocol; retain the
        // click information for future platform code without colliding with
        // L1/R1 (0x0004/0x0008).
        if ((reading.Buttons & GamepadButtons::LeftThumbstick) != GamepadButtons::None)  bits |= 0x0200u; // L3
        if ((reading.Buttons & GamepadButtons::RightThumbstick) != GamepadButtons::None) bits |= 0x0400u; // R3

        // Menu / View
        if ((reading.Buttons & GamepadButtons::Menu) != GamepadButtons::None) bits |= 0x0800u; // Start
        if ((reading.Buttons & GamepadButtons::View) != GamepadButtons::None) bits |= 0x0100u; // Select

        s_padBits[port] = bits;
    }
}

uint16_t GamepadUwp::GetPadBits(int port)
{
    if (port < 0 || port > 1) return 0;
    uint16_t keyboard = 0;
    if (port == 0)
    {
        keyboard = s_keyboardBits.load() | s_keyboardPulse.load();
        // A keyboard tap can begin and end between two PS1 VBlanks. Keep its
        // edge visible for a few polls, matching a real controller press.
        if (s_keyboardPulseFrames.fetch_sub(1) <= 1)
        {
            s_keyboardPulse.store(0);
            s_keyboardPulseFrames.store(0);
        }
    }
    return static_cast<uint16_t>(s_padBits[port] | keyboard);
}

void GamepadUwp::SetKeyboardKey(int virtualKey, bool down)
{
    uint16_t bit = 0;
    switch (virtualKey)
    {
    case 0x26: case 'W': bit = 0x1000u; break;
    case 0x27: case 'D': bit = 0x2000u; break;
    case 0x28: case 'S': bit = 0x4000u; break;
    case 0x25: case 'A': bit = 0x8000u; break;
    case 0x0D: case 'Z': bit = 0x0040u; break;
    case 0x1B: case 'X': bit = 0x0020u; break;
    case 0x20: case 'C': bit = 0x0080u; break;
    case 'V': bit = 0x0010u; break;
    case 'Q': bit = 0x0004u; break;
    case 'E': bit = 0x0008u; break;
    case 0x08: case 'P': bit = 0x0800u; break;
    }
    if (!bit) return;
    if (down)
    {
        s_keyboardBits.fetch_or(bit);
        s_keyboardPulse.fetch_or(bit);
        s_keyboardPulseFrames.store(4);
    }
    else s_keyboardBits.fetch_and(static_cast<uint16_t>(~bit));
}

bool GamepadUwp::IsConnected(int port)
{
    if (port < 0 || port > 1) return false;
    return s_connected[port];
}

uint16_t GamepadUwp::GetGamePadBits(int port)
{
    // Platform_Pad supplies a PS1 serial-pad packet.  The game-facing values
    // used by ImGui (input.h) must be translated before libetc writes them to
    // that packet; otherwise Start becomes R1, A becomes Down, and so on.
    // R3 is reserved for the UWP overlay and is never passed to the core.
    const uint16_t gameBits = s_uiCapture ? 0 : static_cast<uint16_t>(GetPadBits(port) & ~0x0400u);
    return UwpPadMapping::ToPs1Wire(gameBits);
}

void GamepadUwp::SetUiCapture(bool capture)
{
    s_uiCapture = capture;
}
