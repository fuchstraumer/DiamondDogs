// STDAFX.H - used for precompilation of headers used everywhere in project
#pragma once

#pragma warning(push, 0)
#include <stdio.h>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <array>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <stdlib.h>  
#include <iostream>
#include <chrono>
#include <forward_list>
#include <future>

#define GLFW_DLL
#define GLFW_INCLUDE_VULKAN
#include "glfw\glfw3.h"
#ifdef _WIN32
// Allows for easier hooking of window/context with ImGui.
#undef APIENTRY
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#include "GLFW\glfw3native.h"
#endif

// hash used to allow inserting glm vec's into unordered containers.
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_SWIZZLE
#include "glm\glm.hpp"
#include "glm\gtc\matrix_transform.hpp"
#include "glm\gtc\type_ptr.hpp"
#include "glm\gtc\quaternion.hpp"
#include "glm\gtx\hash.hpp"

// Both of these have silly errors we can ignore.

#include "gli\gli.hpp"
#include "engine\util\lodepng.h"

// Forces assert even in release mode: convienient for catching
// errors that occur due to release optimizations.
#define  VK_FORCE_ASSERT
#include "vulkan/vulkan.h"

#pragma warning(pop)

// Number of odd/broken defines in this, as it includes windows.h 
#define NOMINMAX
#include "engine\util\easylogging++.h"
#undef NOMINMAX

// Default width/height of window. Should probably move this elsewhere and/or remove it entirely.
constexpr uint32_t DEFAULT_WIDTH = 1920, DEFAULT_HEIGHT = 1080;


