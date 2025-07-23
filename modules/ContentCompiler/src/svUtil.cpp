#include "svUtil.hpp"
#include <charconv>
#include <string>

namespace svutil
{
    std::string to_lowercase(std::string_view& view)
    {
        std::string result;
        result.reserve(view.size());
        for (size_t i = 0; i < view.size(); ++i)
        {
            result.push_back(std::tolower(view[i]));
        }
        result.shrink_to_fit();
        return result;
    }

    auto get_line(std::string_view& view)->std::string_view
    {
        if (view.empty())
        {
            return std::string_view{};
        }

        const size_t firstNewline = view.find_first_of('\n');
        const size_t firstReturn = view.find_first_of('\r');
        if (firstReturn != std::string::npos)
        {
            // We probably have both, but in this case we need to return the line
            // w/o the return but cut out the return in the parent view
            std::string_view result_view = view.substr(0, firstReturn);
            view.remove_prefix(firstNewline + 1);
            return result_view;
        }
        else
        {
            std::string_view result_view = view.substr(0, firstNewline);
            view.remove_prefix(firstNewline + 1);
            return result_view;
        }
    }

    std::vector<std::string_view> separate_tokens_in_view(std::string_view view, const char delimiter)
    {
        std::vector<std::string_view> results;
        while (!view.empty())
        {
            size_t distanceToDelimiter = view.find_first_of(delimiter);
            if (distanceToDelimiter != std::string::npos && distanceToDelimiter > 0)
            {
                results.emplace_back(view.substr(0, distanceToDelimiter));
                view.remove_prefix(distanceToDelimiter + 1);
            }
            else if (distanceToDelimiter == 0)
            {
                view.remove_prefix(1);
            }
            else
            {
                results.emplace_back(view);
                break;
            }
        }
        // We assume tokens here are split by spaces
        return results;
    }
}