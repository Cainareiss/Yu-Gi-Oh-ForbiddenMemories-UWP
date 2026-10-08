#include "FrontendGamepadInput.h"
#include "imgui_internal.h"
#include <cassert>
#include <cstdio>

static FrontendGamepadInput input;
static ImGuiID playId, modsId, settingsId;
static bool first = true, modsOpen = false, settingsOpen = false, checked = false;
static int scale = 0, plays = 0;

static void Frame(uint16_t bits = 0, bool connected = true)
{
    input.Update(bits, connected, false, false);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(800, 600));
    if (first) ImGui::SetNextWindowFocus();
    ImGui::Begin("Launcher", nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGui::Button("Select game");
    playId = ImGui::GetID("Play");
    if (ImGui::Button("Play")) ++plays;
    if (first) { ImGui::SetKeyboardFocusHere(-1); first = false; }
    modsId = ImGui::GetID("Mods");
    if (ImGui::Button("Mods")) { modsOpen = true; ImGui::OpenPopup("Mods options"); }
    settingsId = ImGui::GetID("Settings");
    if (ImGui::Button("Settings")) { settingsOpen = true; ImGui::OpenPopup("Settings options"); }
    if (ImGui::BeginPopupModal("Mods options", &modsOpen))
    {
        ImGui::Checkbox("Enable mod", &checked);
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))
        { modsOpen = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Settings options", &settingsOpen))
    {
        const char* options[] = { "1x", "2x", "4x" };
        ImGui::Combo("Scale", &scale, options, 3);
        ImGui::SetItemDefaultFocus();
        if (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))
        { settingsOpen = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::End();
    ImGui::Render();
}

static void Settle() { for (int i = 0; i < 5; ++i) Frame(); }
static void Press(uint16_t bits) { Frame(bits); Frame(bits); Settle(); }

int main()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800, 600);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigNavCursorVisibleAlways = true;
    io.ConfigNavEscapeClearFocusItem = false;
    unsigned char* pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

    Settle();
    assert(io.BackendFlags & ImGuiBackendFlags_HasGamepad);
    assert(GImGui->NavId == playId);
    Press(0x4000); // D-pad / left stick down, with no pointer input.
    assert(GImGui->NavId == modsId);
    Press(0x0040); // A opens the focused modal.
    assert(modsOpen && GImGui->NavWindow->Flags & ImGuiWindowFlags_Modal);
    Press(0x0040);
    assert(checked && plays == 0);
    Press(0x0020); // B closes the frontend modal and restores focus.
    assert(!modsOpen && GImGui->NavId == modsId);
    Press(0x4000);
    assert(GImGui->NavId == settingsId);
    Press(0x0040);
    assert(settingsOpen);
    Press(0x0040); // Open resolution combo.
    Press(0x4000);
    Press(0x0040);
    assert(scale == 1);
    Press(0x0020);
    assert(!settingsOpen && GImGui->NavId == settingsId);
    Frame(0, false);
    assert(!(io.BackendFlags & ImGuiBackendFlags_HasGamepad));
    Settle();
    assert(io.BackendFlags & ImGuiBackendFlags_HasGamepad);

    FrontendGamepadInput game;
    assert(!game.Update(0x0020, true, true, false)); // B never opens menu.
    assert(!game.Update(0x0800, true, true, false)); // Start never opens menu.
    assert(!game.Update(0x0900, true, true, false)); // Start + View also does not.
    assert(game.Update(0x0400, true, true, false));  // R3 opens menu once.
    assert(!game.Update(0x0400, true, true, true)); // Holding does not toggle again.
    assert(!game.Update(0, true, true, true));
    assert(game.Update(0x0400, true, true, true)); // Next R3 closes it.
    assert(!game.Update(0, true, false, false));
    assert(!game.Update(0x0400, true, false, false)); // R3 ignored in launcher.
    assert(!game.Update(0, false, true, false));
    assert(game.Update(0x0400, true, true, false)); // Reconnection recovers input.
    assert(!io.ConfigNavMoveSetMousePos && !io.MouseDrawCursor);
    ImGui::DestroyContext();
    std::puts("PASS: gamepad focus, A/B, modal isolation, resolution combo, reconnect, R3-only menu and hold debounce");
}
