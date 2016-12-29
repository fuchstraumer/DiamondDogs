// STDAFX.H - used for precompilation of headers used everywhere in project
#pragma once

#include <stdio.h>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <array>

using uint = unsigned int;
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>  
#include <crtdbg.h>  
#include <iostream>


#ifdef _DEBUG
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

#define GLEW_STATIC
#include "GL\glew.h"

#define GLFW_DLL
#include "glfw\glfw3.h"

#define GLM_SWIZZLE
#include "glm\glm.hpp"
#include "glm\gtc\matrix_transform.hpp"
#include "glm\gtc\type_ptr.hpp"
#include "glm\gtc\quaternion.hpp"

// GLFW declarations and definitions
const GLuint SCR_WIDTH = 1440, SCR_HEIGHT = 900;
// Defines depth rendering ranges for projection matrix.
const GLfloat nearDepth = 0.1f, farDepth = 5000.0f;

