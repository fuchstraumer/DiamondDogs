#include "content/Key.hpp"
#include <cstdint>
#include <span>

namespace
{
    void WriteWordHex(uint64_t word, char* out) noexcept;
}

namespace foundation::content
{
    void WriteHexWords(uint64_t high, uint64_t low, std::span<char, 32> out) noexcept
    {
        WriteWordHex(high, out.data());
        WriteWordHex(low, out.data() + 16);
    }
}

namespace
{
    void WriteWordHex(uint64_t word, char* out) noexcept
    {
        constexpr char digits[] = "0123456789abcdef";

        for (int32_t nibble = 0; nibble < 16; ++nibble)
        {
            const int32_t shift = 60 - 4 * nibble;
            const uint64_t digit = (word >> shift) & 0xF;
            out[nibble] = digits[digit];
        }
    }
}
