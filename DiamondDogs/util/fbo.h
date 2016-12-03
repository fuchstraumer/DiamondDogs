#pragma once
#ifndef FBO_H
#define FBO_H
#include "../stdafx.h"
// LodeTexture includes FBO texture classes
#include "lodeTexture.h"

class FBO {
public:
	// Handle that refers to this FBO
	GLuint Handle;
	// Two primary textures
	FBO_Texture2D Tex;
	FBO_DepthTexture2D DepthTex;
	

	FBO(const GLuint& tex, const GLuint& depthtex = 0) {
		glGenFramebuffers(1, &Handle);
		glBindFramebuffer(GL_FRAMEBUFFER, Handle);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
		if (depthtex) {
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthtex, 0);
		}
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			std::cerr << "FBO error: " << glCheckFramebufferStatus(GL_FRAMEBUFFER) << std::endl;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}


	FBO(unsigned int width, unsigned int height, bool depth) {
		glGenFramebuffers(1, &Handle);
		glBindFramebuffer(GL_FRAMEBUFFER, Handle);
		Tex = FBO_Texture2D(width, height);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Tex.gl_Handle, 0);
		if (depth) {
			DepthTex = FBO_DepthTexture2D(width, height);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, DepthTex.gl_Handle, 0);
		}
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			std::cerr << "FBO error: " << glCheckFramebufferStatus(GL_FRAMEBUFFER) << std::endl;
		}
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	~FBO() = default;
};

#endif // !FBO_H
