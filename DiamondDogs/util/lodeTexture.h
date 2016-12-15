#pragma once
#ifndef LODE_TEXTURE_H
#define LODE_TEXTURE_H
#include "lodepng.h"
#include "../stdafx.h"
#include "shader.h"
#include <unordered_map>

using uint = unsigned int;

// Baseline texture class

class Texture {
public:
	Texture() = default;

	~Texture() {
		glDeleteTextures(1, &gl_Handle);
	}

	// Handle used to bind this texture
	GLuint gl_Handle = -1;
	// Dimensions of the texture
	uint Width, Height;
	// Texture unit this texture uses (1-48)
	GLenum TextureUnit;

	// Once this function is called the texture is ready for use
	virtual void BuildTexture(){ }
	// Once this function is called the texture WILL be used/active
	virtual void BindTexture() const { }

	Texture& operator=(Texture&& other) {
		this->gl_Handle = other.gl_Handle;
		return *this;
	}

	Texture(Texture&& other) : gl_Handle(other.gl_Handle) {
	}

	Texture& operator=(const Texture& other) {
		this->gl_Handle = other.gl_Handle;
		return *this;
	}

	Texture(const Texture& other) : gl_Handle(other.gl_Handle) {
	}

protected:
	// List of files to import
	std::vector<std::string> fileList;
	// Vector containing the actual texture data output by lodepng
	std::vector<unsigned char*> textureData;
};

// Builds and loads an OpenGL 3D texture for use as a 2D texture array.
// To use in a program, call glBindTexture(texture->GL_Handle)
class TextureArray : public Texture {
public:

	// The input vector contains the filenames to use for importing
	TextureArray(std::vector<std::string> filelist, uint textureDims) {
		glGenTextures(1, &this->gl_Handle);
		this->depth = (GLsizei)filelist.size();
		this->fileList = filelist;
		Width = textureDims;
		Height = textureDims;
	}

	// Builds the texture array: pass in an active texture unit for this 
	// array to assign to, and then active the array by calling glBindTexture(
	// TextureArray->GL_Handle);
	virtual void BuildTexture() override {
		for (auto str : this->fileList) {
			unsigned char* data;
			lodepng_decode32_file(&data, &Width, &Height,
				str.data());
			this->textureData.push_back(data);
		}
		GLenum err;
		glGenTextures(1, &this->gl_Handle);
		err = glGetError();
		glBindTexture(GL_TEXTURE_2D_ARRAY, this->gl_Handle);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, Width, Height,
			this->depth, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		for (GLsizei i = 0; i < this->depth; ++i) {
			glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, Width, Height, 1,
				GL_RGBA, GL_UNSIGNED_BYTE, this->textureData[i]);
		}
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);	// Set texture wrapping to GL_REPEAT
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
		glBindTexture(GL_TEXTURE, 0);
	}

	virtual void BindTexture() const override {
		glBindTexture(GL_TEXTURE_2D_ARRAY, gl_Handle);
	}

private:

	GLsizei depth; // Sets amount of slots to reserve in the texture array
};


// Builds a cubemap texture, for use as a skybox.
class CubemapTexture : public Texture {
public:

	// The handle used to activate this texture
	CubemapTexture(std::vector<std::string> filelist, uint texture_dims) : Texture() {
		Width = texture_dims;
		Height = texture_dims;
		fileList = filelist;
	}

	virtual void BuildTexture() override {
		for (auto str : fileList) {
			unsigned char* data;
			lodepng_decode32_file(&data, &Width, &Height, str.data());
			textureData.push_back(data);
		}
		glGenTextures(1, &this->gl_Handle);
		glBindTexture(GL_TEXTURE_CUBE_MAP, gl_Handle);

		for (uint i = 0; i < 6; ++i) {
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureData[i]);
		}

		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
	}

	virtual void BindTexture() const override {
		glBindTexture(GL_TEXTURE_CUBE_MAP, gl_Handle);
	}
};

class NormalCubemap : public Texture {
public:

};

class HeightCubemap : public Texture {

};

class BumpCubemap : public Texture {

};

class Texture1D : public Texture{
public:
	Texture1D(const char* file, uint texture_size) : Texture() {
		Width = texture_size;
		fileList.push_back(file);
	}

	virtual void BuildTexture() override {
		unsigned char* data;
		uint height = 1;
		lodepng_decode32_file(&data, &Width, &height, fileList[0].data());
		textureData.push_back(data);
		glGenTextures(1, &gl_Handle);
		glBindTexture(GL_TEXTURE_1D, gl_Handle);

		glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, Width, 0, GL_RGBA, GL_UNSIGNED_BYTE, textureData[0]);

		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glGenerateMipmap(GL_TEXTURE_1D);
		glBindTexture(GL_TEXTURE_1D, 0);
	}

	virtual void BindTexture() const override {
		glBindTexture(GL_TEXTURE_1D, gl_Handle);
	}
};

class Texture2D : public Texture {
public:
	Texture2D(std::string filename, uint width, uint height) : Texture() {
		Width = width;
		Height = height;
		fileList.push_back(filename);
	}

	Texture2D() = default;

	virtual void BuildTexture() override {
		unsigned char* data;
		lodepng_decode32_file(&data, &Width, &Height, fileList[0].data());
		glGenTextures(1, &gl_Handle);
		glBindTexture(GL_TEXTURE_2D, gl_Handle);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	virtual void BindTexture() const override {
		glBindTexture(GL_TEXTURE_2D, gl_Handle);
	}
};

class FBO_Texture2D : public Texture {
public:

	FBO_Texture2D() : Texture() { }

	FBO_Texture2D(uint width, uint height) : Texture() {
		glGenTextures(1, &gl_Handle);
		glBindTexture(GL_TEXTURE_2D, gl_Handle);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_FLOAT, NULL);

		//texture parameters
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	virtual void BindTexture() const override {
		glBindTexture(GL_TEXTURE_2D, gl_Handle);
	}
};

class EmptyTexture2D : public Texture {
public:

	EmptyTexture2D() : Texture() { }

	EmptyTexture2D(uint width, uint height) : Texture() {
		glGenTextures(1, &gl_Handle);
		glBindTexture(GL_TEXTURE_2D, gl_Handle);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_FLOAT, NULL);

		//texture parameters
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	virtual void BindTexture() const override {
		glBindTexture(GL_TEXTURE_2D, gl_Handle);
	}
};

class FBO_DepthTexture2D : public Texture {
public:

	FBO_DepthTexture2D() : Texture() {}

	FBO_DepthTexture2D(uint width, uint height) : Texture() {
		glGenTextures(1, &gl_Handle);
		glBindTexture(GL_TEXTURE_2D, gl_Handle);
		// Generate empty texture - this is an FBO, so we're going to write to it eventually
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

		//texture parameters
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glBindTexture(GL_TEXTURE_2D, 0);
	}

	virtual void BindTexture() const override {
		glBindTexture(GL_TEXTURE_2D, gl_Handle);
	}
};

using TextureRef = std::reference_wrapper<const Texture>;
using TextureMap = std::unordered_map<std::string, GLuint>;

class TextureManager {
public:
	// Vector that stores references to textures.
	std::vector<TextureRef> TextureRefs;
	// Unordered map that stores and retrieves textures.
	// Key type is a chosen name for the texture.
	TextureMap Map;
	// Used to track how many textures are active and as such
	// what texture unit should be used when binding and building
	// new textures
	GLuint ActiveCount;

	// Adds a texture to the map
	void AddTexture(Texture& tex, const std::string& alias) {
		using map_entry = std::pair<std::string, GLuint>;
		TextureRefs.push_back(std::cref(tex));
		map_entry newEntry(alias, tex.gl_Handle);
		Map.insert(newEntry);
		ActiveCount++;
	}

	// Returns handle to texture with alias
	GLuint GetTexture(const std::string& alias) {
		return Map.at(alias);
	}
};

#endif // !LODE_TEXTURE_H
