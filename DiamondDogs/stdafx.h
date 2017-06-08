// STDAFX.H - used for precompilation of headers used everywhere in project
#pragma once
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
#include <regex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <stdlib.h>  
#include <iostream>
#include <chrono>
#include <forward_list>
#define GLFW_DLL
#define GLFW_INCLUDE_VULKAN
#include "glfw\glfw3.h"
#ifdef _WIN32
#undef APIENTRY
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#include "GLFW\glfw3native.h"
#endif
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_SWIZZLE
#include "glm\glm.hpp"
#include "glm\gtc\matrix_transform.hpp"
#include "glm\gtc\type_ptr.hpp"
#include "glm\gtc\quaternion.hpp"
#include "glm\gtx\hash.hpp"
#pragma warning(push, 0)
#include "gli\gli.hpp"
#pragma warning(pop)
#define  VK_FORCE_ASSERT
#include "vulkan/vulkan.h"
#include "common/CreateInfoBase.h"
#include "common/vkAssert.h"
#include "common/vk_constants.h"

#include "common\CommonDef.h"

#define NOMINMAX
#include "engine\util\easylogging++.h"
#undef NOMINMAX

#include "engine\util\lodepng.h"

// GLFW declarations and definitions
constexpr uint32_t DEFAULT_WIDTH = 1920, DEFAULT_HEIGHT = 1080;
// Defines depth rendering ranges for projection matrix.
constexpr float nearDepth = 1.0f, farDepth = 500000.0f;


