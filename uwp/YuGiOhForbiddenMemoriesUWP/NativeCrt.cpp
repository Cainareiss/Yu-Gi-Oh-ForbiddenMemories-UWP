#include <cstdint>
#include <cstring>

// UCRT exposes fabsf as a header inline. Native mods also need an address
// for the C ABI export, so provide its IEEE-754 operation without changing
// the host's math header definitions or calling a different CRT.
extern "C" float Memories_UWP_Fabsf(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t), "IEEE-754 float required");
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    bits &= 0x7fffffffu;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
