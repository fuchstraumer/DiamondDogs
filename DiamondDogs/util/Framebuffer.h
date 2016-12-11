#pragma once
#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H
#include "../stdafx.h"
#include "../util/lodeTexture.h"
/*
	FRAMEBUFFER_H

	Defines base framebuffer abstract class and the three primary types of framebuffers
	that will be commonly used:

	- Color attachment FBO

	- Depth attachment FBO

	- Stencil attachment FBO

	- Depth stencil attachment FBO

	The FBO base class just defines the base methods of generating the main openGL handle
	used to bind or modify the FBO, along with the proper destructor function

*/

class Framebuffer {
public:
	Framebuffer() = default;

	Framebuffer(unsigned int width,unsigned int height, bool depth_texture = false) {
		glGenFramebuffers(1, &gl_Handle);
		glBindFramebuffer(GL_FRAMEBUFFER, gl_Handle);
		Width = width;
		Height = height;
		fbo_Texture = FBO_Texture2D(width, height);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_Texture.gl_Handle, 0);
		if (depth_texture) {
			fbo_DepthTexture = FBO_DepthTexture2D(width, height);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, fbo_DepthTexture.gl_Handle, 0);
		}
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			std::cerr << "Framebuffer incomplete:" << glCheckFramebufferStatus(GL_FRAMEBUFFER) << std::endl;
			throw("FBO status error");
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}
	~Framebuffer() {
		if (gl_Handle != -1) {
			glDeleteFramebuffers(1, &gl_Handle);
		}
	}

	void Use() {
		glBindFramebuffer(GL_FRAMEBUFFER, gl_Handle);
	}

	void BuildFBO();


	// Height and width of this FBO
	unsigned int Height, Width;
protected:
	GLuint gl_Handle;
	FBO_Texture2D fbo_Texture;
	FBO_DepthTexture2D fbo_DepthTexture;
};

#endif // !FRAMEBUFFER_H
