#pragma once
#ifndef VULPES_FRAMEBUFFER_H
#define VULPES_FRAMEBUFFER_H
#include "stdafx.h"
#include "device_object.h"
namespace vulpes {

	template<size_t cnt>
	struct framebuffer : device_object<named_framebuffer_tag, cnt> {
		framebuffer(bool use_depth_component = false) : device_object() {
			color_attachments();
			for (size_t i = 0; i < cnt; ++i) {
				glNamedFramebufferTexture(handles[i], GL_COLOR_ATTACHMENT0, color_attachments[i], 0);
			}
			if (use_depth_component) {
				depth_attachments();
				for (size_t i = 0; i < cnt; ++i) {
					glNamedFramebufferTexture(handles[i], GL_DEPTH_ATTACHMENT, depth_attachments[i], 0);
				}
			}
			if (std::any_of(handles.cbegin(), handles.cend(), [](const GLuint& handle) { return glCheckNamedFramebufferStatus(handle, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE; }) {
				throw;
			}
		}

		device_object<texture_2d_tag, cnt> color_attachments;
		device_object<texture_2d_tag, cnt> depth_attachments;
	};

}
#endif // !VULPES_FRAMEBUFFER_H
