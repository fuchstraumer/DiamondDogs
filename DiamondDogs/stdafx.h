// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>
#include <string>
#include <vector>
#include <iostream>

using uint = unsigned int;

#define GLEW_STATIC
#include "GL\glew.h"

#define GLFW_DLL
#include "glfw\glfw3.h"

#define GLM_SWIZZLE
#include "glm\glm.hpp"
#include "glm\gtc\matrix_transform.hpp"
#include "glm\gtc\type_ptr.hpp"

// GLFW declarations and definitions
const GLuint SCR_WIDTH = 1440, SCR_HEIGHT = 900;

const int SUN_RADIUS = 400;



// TODO: reference additional headers your program requires here
