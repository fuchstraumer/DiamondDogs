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
#include <memory>

using uint = unsigned int;
#include <stdlib.h>  
#include <iostream>

// OpenGL includes.

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
const GLuint SCR_WIDTH = 1920, SCR_HEIGHT = 1080;
// Defines depth rendering ranges for projection matrix.
const GLfloat nearDepth = 1.0f, farDepth = 500000.0f;

