#pragma once
#ifndef DIAMOND_DOGS_CMAKE_PCH_DIAMOND_DOGS_PCH_HPP
#define DIAMOND_DOGS_CMAKE_PCH_DIAMOND_DOGS_PCH_HPP

// Precompiled header for every DiamondDogs target. It contains standard library headers, DirectXMath and the Vulkan core
// header only. It includes none of our own headers, so an edit to one of them never rebuilds the PCH.
// A PCH hides missing includes: configure with -DDIAMOND_DOGS_USE_PCH=OFF to check that each file includes what it uses.

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <DirectXMath.h>
#include <vulkan/vulkan_core.h>

#endif // !DIAMOND_DOGS_CMAKE_PCH_DIAMOND_DOGS_PCH_HPP
