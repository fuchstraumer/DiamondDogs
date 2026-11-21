#pragma once
#ifndef DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_KEY_HPP
#define DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_KEY_HPP
#include "freeze/FreezeContext.hpp"
#include <cstdint>
#include <source_location>
#include <string_view>
#include <vector>

// The one lookup that every resolve walk ends in, and its typed wrappers (LM103, LM104, LM107).
namespace loom::freeze
{

    int32_t ResolveKey(FreezeContext& context, KeyRef key, KeyClass expected_kind, std::source_location where);

    ResourceIndex ResolveResource(FreezeContext& context, KeyRef key, KeyClass expected_kind,
                                  std::source_location where);

    AnchorIndex ResolveAnchor(FreezeContext& context, KeyRef key, std::source_location where);

}

#endif // !DIAMOND_DOGS_LOOM_FREEZE_RESOLVE_KEY_HPP
