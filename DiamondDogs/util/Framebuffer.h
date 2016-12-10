#pragma once
#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include "../stdafx.h"

/*
	FRAMEBUFFER_H

	Defines base framebuffer abstract class and the three primary types of framebuffers
	that will be commonly used:

	- Color attachment FBO

	- Depth attachment FBO

	- Depth stencil attachment FBO

	The FBO base class just defines the base methods of generating the main openGL handle
	used to bind or modify the FBO, along with the proper destructor function

*/

class Framebuffer {
public:
	Framebuffer() = default;
	~Framebuffer() = default;

	// Height and width of this FBO
	unsigned int Height, Width;
protected:
	GLuint Handle;
};
#endif // !FRAMEBUFFER_H
