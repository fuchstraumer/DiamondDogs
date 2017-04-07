#pragma once
#ifndef CORONA_H
#define CORONA_H
#include "stdafx.h"
#include "../../engine/rendering/Shader.h"
#include "../../engine/mesh/Billboard.h"
#include "../../util/lodeTexture.h"

// Structure defining a stars corona
struct Corona {

	Corona() : Blackbody("./rsrc/img/star/star_spectrum.png", 1024) {}

	Corona(const Corona& other) = delete;
	Corona& operator=(const Corona& other) = delete;

	Corona(Corona&& other) : Blackbody(std::move(other.Blackbody)), coronaProgram(std::move(other.coronaProgram)), mesh(std::move(other.mesh)), frame(std::move(other.frame)) {}

	Corona& operator=(Corona&& other) {
		Blackbody = std::move(other.Blackbody);
		coronaProgram = std::move(other.coronaProgram);
		mesh = std::move(other.mesh);
		frame = std::move(other.frame);
		return *this;
	}

	~Corona() {
		delete coronaProgram;
	}

	Corona(const glm::vec3& position, const float& radius) : Blackbody("./rsrc/img/star/star_spectrum.png", 1024) {
		coronaProgram = new ShaderProgram();
		coronaProgram->Init();
		Shader cVert("./shaders/billboard/corona_vertex.glsl", VERTEX_SHADER);
		Shader cFrag("./shaders/billboard/corona_fragment.glsl", FRAGMENT_SHADER);
		coronaProgram->AttachShader(cVert);
		coronaProgram->AttachShader(cFrag);
		coronaProgram->CompleteProgram();
		// Setup uniforms for billboard
		std::vector<std::string> uniforms{
			"view",
			"projection",
			"model",
			"normTransform",
			"center",
			"size", // Size of corona in world-space units.
			"cameraUp",
			"cameraRight",
			"frame",
			"temperature",
			"blackbody",
		};
		coronaProgram->BuildUniformMap(uniforms);
		
		mesh.Scale = glm::vec3(6 * radius);
		mesh.Radius = 6.0f * radius;
		mesh.Position = position;
		mesh.Angle = glm::vec3(0.0f);
		Blackbody.BuildTexture();
	}

	void BuildRenderData(const int& star_temperature) {
		// Set frame counter to zero
		frame = 0;
		GLuint tempLoc = coronaProgram->GetUniformLocation("temperature");
		glUniform1i(tempLoc, star_temperature);
		GLuint texLoc = coronaProgram->GetUniformLocation("blackbody");
		glUniform1i(texLoc, 0);
		mesh.Program = coronaProgram;
		mesh.BuildRenderData();
	}

	void Render(const glm::mat4 & view, const glm::mat4& projection) {
		glActiveTexture(GL_TEXTURE3);
		Blackbody.BindTexture();
		coronaProgram->Use();
		// If frame counter is equal to limits of numeric precision,
		if (frame == std::numeric_limits<GLint>::max()) {
			// Reset frame counter
			frame = 0;
		}
		// Otherwise, increment it
		else {
			frame++;
		}
		// Set frame value in Program
		GLuint frameLoc = coronaProgram->GetUniformLocation("frame");
		glUniform1i(frameLoc, static_cast<GLint>(frame));
		mesh.Render(view, projection);
	}

	Billboard3D mesh;
	ShaderProgram* coronaProgram;
	ldtex::Texture1D Blackbody;
	uint64_t frame;
};


#endif // !CORONA_H
