#include "UwpPadMapping.h"
#include <cassert>
#include <cstdio>

int main()
{
    using UwpPadMapping::ToPs1Wire;
    const auto gameDecode = [](uint16_t wire) {
        const uint8_t packetLow = static_cast<uint8_t>(~wire);
        const uint8_t packetHigh = static_cast<uint8_t>(~(wire >> 8));
        return static_cast<uint16_t>((static_cast<uint16_t>(packetLow) << 8 | packetHigh) ^ 0xFFFFu);
    };

    // These are the exact bytes libetc puts into a PS1 packet. Input_ReadRawPads
    // then converts them back to the game masks used by input.h.
    assert(ToPs1Wire(0x1000) == 0x0010); // Up
    assert(ToPs1Wire(0x2000) == 0x0020); // Right
    assert(ToPs1Wire(0x4000) == 0x0040); // Down
    assert(ToPs1Wire(0x8000) == 0x0080); // Left
    assert(ToPs1Wire(0x0040) == 0x4000); // Xbox A -> Cross
    assert(ToPs1Wire(0x0020) == 0x2000); // Xbox B -> Circle
    assert(ToPs1Wire(0x0080) == 0x8000); // Xbox X -> Square
    assert(ToPs1Wire(0x0010) == 0x1000); // Xbox Y -> Triangle
    assert(ToPs1Wire(0x0800) == 0x0008); // Xbox Menu -> Start
    assert(ToPs1Wire(0x0100) == 0x0001); // Xbox View -> Select
    assert(ToPs1Wire(0x000F) == 0x0F00); // triggers and shoulders
    assert(ToPs1Wire(0xFFFF) == 0xFFFF); // complete controller state

    // This is exactly libetc.c -> Input_ReadRawPads(): write a PS1 packet,
    // then decode it into the masks the retail game uses.
    for (uint32_t bit = 1; bit <= 0x8000; bit <<= 1)
        assert(gameDecode(ToPs1Wire(static_cast<uint16_t>(bit))) == bit);
    assert(gameDecode(ToPs1Wire(0xFFFF)) == 0xFFFF);

    // Repeated Xbox A and Menu presses are edge-sensitive in the game.  Run
    // them as a real press/release stream so a future mapping change cannot
    // turn either into a shoulder button or leave it latched.
    for (int i = 0; i != 1000; ++i) {
        assert(gameDecode(ToPs1Wire(0x0040)) == 0x0040); // A -> Cross
        assert(gameDecode(ToPs1Wire(0x0000)) == 0x0000);
        assert(gameDecode(ToPs1Wire(0x0800)) == 0x0800); // Menu -> Start
        assert(gameDecode(ToPs1Wire(0x0000)) == 0x0000);
    }

    std::puts("PASS: UWP game-facing pad bits map to the PS1 wire protocol");
}
