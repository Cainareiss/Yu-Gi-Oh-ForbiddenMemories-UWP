#pragma once
#include "imgui.h"
#include <cstdint>

// Shared input adapter: platform polling never emulates mouse movement.
class FrontendGamepadInput
{
public:
    bool Update(uint16_t bits, bool connected, bool inGame, bool overlayOpen)
    {
        ImGuiIO& io = ImGui::GetIO();
        if (connected) io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        else io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        if (!connected) bits = 0;
        const uint16_t pressed = bits & ~m_previous;
        m_previous = bits;

        // Menu input is released during gameplay, while B/Start remain
        // available to the PS1 core. R3 is reserved for the overlay.
        const uint16_t nav = (!inGame || overlayOpen) ? bits : 0;
        io.AddKeyEvent(ImGuiKey_GamepadDpadUp, (nav & 0x1000) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadDpadDown, (nav & 0x4000) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (nav & 0x8000) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (nav & 0x2000) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadFaceDown, (nav & 0x0040) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadFaceRight, (nav & 0x0020) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadFaceUp, (nav & 0x0010) != 0);
        io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, (nav & 0x0080) != 0);
        return inGame && (pressed & 0x0400) != 0;
    }
private:
    uint16_t m_previous = 0;
};
