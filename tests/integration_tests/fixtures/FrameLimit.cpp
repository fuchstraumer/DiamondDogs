#include "FrameLimit.hpp"
#include <charconv>
#include <iostream>
#include <string_view>
#include <system_error>

int32_t ParseFrameLimit(int argc, char* argv[])
{
    constexpr std::string_view framesFlag{ "--frames=" };
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument{ argv[i] };
        if (!argument.starts_with(framesFlag))
        {
            continue;
        }
        const std::string_view value = argument.substr(framesFlag.size());
        int32_t frameLimit = -1;
        const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), frameLimit);
        if (parsed.ec != std::errc{})
        {
            std::cerr << "Ignoring malformed " << argument << ", running until the window closes\n";
            return -1;
        }
        return frameLimit;
    }
    return -1;
}
