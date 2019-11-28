#pragma once
#ifndef CONTENT_COMPILER_STRING_VIEW_UTILITY_HPP
#define CONTENT_COMPILER_STRING_VIEW_UTILITY_HPP
#include <string_view>
#include <vector>

namespace svutil
{

    std::string to_lowercase(std::string_view& view);
    auto get_line(std::string_view& view)->std::string_view;
    std::vector<std::string_view> separate_tokens_in_view(std::string_view view, const char delimiter);

}

#endif //!CONTENT_COMPILER_STRING_VIEW_UTILITY_HPP