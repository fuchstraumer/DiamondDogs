#pragma once
#ifndef VULPES_TEXTURE_H
#define VULPES_TEXTURE_H
#include "stdafx.h"
#include "device_object.h"
#include "util/lodepng.h"
namespace vulpes {

	static void lodepng_decode(const char* filename, unsigned int width, unsigned int height, std::vector<unsigned char>& dest) {
		unsigned err = lodepng::decode(dest, width, height, filename);
		if (err) {
			std::cerr << "LodePNG decoder error: " << err << " - " << lodepng_error_text(err) << std::endl;
		}
		return;
	}

	struct texture_1d : public device_object<texture_1d_t> {
		texture_1d(const char* filename, unsigned int _size) : device_object(), size(_size) {
			GLuint id = handles[0];
			glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			std::vector<unsigned char> img_data;
			lodepng_decode(filename, size, 1, img_data);
			glTexImage1D(id, 0, GL_RGBA, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, &img_data[0]);
			glGenerateTextureMipmap(id);
		}

		texture_1d(unsigned int _size) : device_object(), size(_size) {
			GLuint id = handles[0];
			glTextureParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTextureParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTextureStorage1D(id, 0, GL_RGBA, size);
		}

		void import_texture_data(const char* filename) {
			std::vector<unsigned char> img_data;
			lodepng_decode(filename, size, 1, img_data);
			set_texture_data(&img_data[0]);
		}

		void set_texture_data(const unsigned char* img_data) {
			glTexImage1D(handles[0], 0, GL_RGBA, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);
			glGenerateTextureMipmap(handles[0]);
		}

		unsigned int size;
	};

	struct texture_2d : public device_object<texture_2d_t> {

		texture_2d(const char *filename, unsigned int _width, unsigned int _height) : device_object(), width(_width), height(_height) {
			GLuint _id = id();
			glTextureParameteri(_id, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTextureParameteri(_id, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTextureParameteri(_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			std::vector<unsigned char> img_data;
			lodepng_decode(filename, width, height, img_data);
			glTexImage2D(_id, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &img_data[0]);
			glGenerateTextureMipmap(_id);
		}

		texture_2d(unsigned int _width, unsigned int _height) : device_object(), width(_width), height(_height) {
			GLuint _id = id();
			glTextureParameteri(_id, GL_TEXTURE_WRAP_S, GL_CLAMP);
			glTextureParameteri(_id, GL_TEXTURE_WRAP_T, GL_CLAMP);
			glTextureParameteri(_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTextureParameteri(_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTextureStorage2D(_id, 0, GL_RGBA, width, height);
		}

		void import_texture_data(const char* filename) {
			std::vector<unsigned char> img_data;
			lodepng_decode(filename, width, height, img_data);
			set_texture_data(&img_data[0]);
		}

		void set_texture_data(const unsigned char* img_data) {
			glTexImage2D(id(), 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img_data);
			glGenerateTextureMipmap(id());
		}

		GLuint id() {
			return handles[0];
		}

		unsigned int width, height;

	};

	struct texture_cubemap : device_object<texture_cubemap_t> {

		texture_cubemap(const std::vector<std::string>& texture_list, const unsigned int& _width, const unsigned int& _height) : device_object(), width(_width), height(_height) {
			GLuint id = handles[0];
			// Reserve space - oddly enough, this is a 2D call but idk it works
			glTextureStorage2D(id, 1, GL_RGBA, width, height);
			glTexParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexParameteri(id, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
			for (uint i = 0; i < 6; ++i) {
				std::vector<unsigned char> img_data;
				lodepng_decode(texture_list[i].data(), width, height, img_data);
				glTextureSubImage3D(id, 0, 0, 0, i, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, &img_data[0]);
				//glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, &texture_data[i][0]);
			}
			glGenerateTextureMipmap(id);
		}

		unsigned int width, height;

	};

}
#endif // !VULPES_TEXTURE_H
