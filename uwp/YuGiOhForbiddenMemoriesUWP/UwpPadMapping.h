#pragma once

#include <cstdint>

// The frontend uses the game-facing representation from src/game/input.h.
// Platform_Pad(), however, feeds the two bytes of a real PS1 controller
// packet.  libetc writes that packet and Input_ReadRawPads() decodes it back
// to the game-facing representation.  Keep the conversion here so the menu
// can use the friendly game bits while the core always receives valid wire
// bits.
namespace UwpPadMapping
{
    constexpr uint16_t ToPs1Wire(uint16_t gameBits)
    {
        uint16_t wire = 0;

        // D-pad: game order -> PS1 digital-pad packet order.
        if (gameBits & 0x1000u) wire |= 0x0010u; // Up
        if (gameBits & 0x2000u) wire |= 0x0020u; // Right
        if (gameBits & 0x4000u) wire |= 0x0040u; // Down
        if (gameBits & 0x8000u) wire |= 0x0080u; // Left

        // Face buttons: A/Cross, B/Circle, X/Square, Y/Triangle.
        if (gameBits & 0x0040u) wire |= 0x4000u;
        if (gameBits & 0x0020u) wire |= 0x2000u;
        if (gameBits & 0x0080u) wire |= 0x8000u;
        if (gameBits & 0x0010u) wire |= 0x1000u;

        // Shoulders, triggers and system buttons.
        if (gameBits & 0x0001u) wire |= 0x0100u; // L2
        if (gameBits & 0x0002u) wire |= 0x0200u; // R2
        if (gameBits & 0x0004u) wire |= 0x0400u; // L1
        if (gameBits & 0x0008u) wire |= 0x0800u; // R1
        if (gameBits & 0x0100u) wire |= 0x0001u; // View / Select
        if (gameBits & 0x0200u) wire |= 0x0002u; // L3
        if (gameBits & 0x0400u) wire |= 0x0004u; // R3
        if (gameBits & 0x0800u) wire |= 0x0008u; // Menu / Start

        return wire;
    }
}
