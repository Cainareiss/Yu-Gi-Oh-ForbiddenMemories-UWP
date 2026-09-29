#pragma once

#include <cstdint>

class GamepadUwp
{
public:
    static void Initialize();
    static void Poll();
    static uint16_t GetPadBits(int port);
    static bool IsConnected(int port);
    static bool IsPauseRequested();
    static void ClearPauseRequest();
};
