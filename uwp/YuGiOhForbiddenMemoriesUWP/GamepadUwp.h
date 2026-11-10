#pragma once

#include <cstdint>

class GamepadUwp
{
public:
    static void Initialize();
    static void Poll();
    static uint16_t GetControllerBits(int port);
    static uint16_t GetPadBits(int port);
    static uint16_t GetGamePadBits(int port);
    static void SetKeyboardKey(int virtualKey, bool down);
    static void SetUiCapture(bool capture);
    static bool IsConnected(int port);
};
