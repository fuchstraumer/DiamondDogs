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
#include <map>
#include <stdlib.h>  
#include <iostream>
#include <chrono>

#define GLFW_INCLUDE_VULKAN
#include "glfw\glfw3.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_SWIZZLE
#include "glm\glm.hpp"
#include "glm\gtc\matrix_transform.hpp"
#include "glm\gtc\type_ptr.hpp"
#include "glm\gtc\quaternion.hpp"
#pragma warning(push, 0)
#include "gli\gli.hpp"
#pragma warning(pop)
#include "vulkan/vulkan.h"
#include "common/CreateInfoBase.h"
#include "common/vkAssert.h"
#include "common/vk_constants.h"

// GLFW declarations and definitions
constexpr uint32_t DEFAULT_WIDTH = 1440, DEFAULT_HEIGHT = 720;
// Defines depth rendering ranges for projection matrix.
constexpr float nearDepth = 1.0f, farDepth = 500000.0f;


