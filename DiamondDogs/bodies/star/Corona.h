#pragma once
#ifndef CORONA_H
#define CORONA_H
#include "stdafx.h"
#include "../../engine/renderer/Shader.h"
#include "../../engine/objects/Billboard.h"
#include "../../util/lodeTexture.h"

// Structure defining a stars corona
struct Corona {

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

	Corona(const float& radius, const glm::vec3& position = glm::vec3(0.0f)) : mesh(radius, position) {
		coronaProgram.Init();
		Shader cVert("./shaders/billboard/corona_vertex.glsl", VERTEX_SHADER);
		Shader cFrag("./shaders/billboard/corona_fragment.glsl", FRAGMENT_SHADER);
		coronaProgram.AttachShader(cVert);
		coronaProgram.AttachShader(cFrag);
		coronaProgram.CompleteProgram();
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
		coronaProgram.BuildUniformMap(uniforms);
	}

	void BuildRenderData(const int& star_temperature) {
		Blackbody = new ldtex::Texture1D("./rsrc/img/star/star_spectrum.png", 1024);
		// Set frame counter to zero
		frame = 0;
		GLuint tempLoc = coronaProgram.GetUniformLocation("temperature");
		glUniform1i(tempLoc, star_temperature);
		GLuint texLoc = coronaProgram.GetUniformLocation("blackbody");
		glUniform1i(texLoc, 0);
		mesh.Program = std::move(coronaProgram);
		mesh.BuildRenderData();
	}

	void Render(const glm::mat4 & view, const glm::mat4& projection) {
		glActiveTexture(GL_TEXTURE3);
		Blackbody->BindTexture();
		mesh.Program.Use();
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
		GLuint frameLoc = mesh.Program.GetUniformLocation("frame");
		glUniform1i(frameLoc, static_cast<GLint>(frame));
		mesh.Render(view, projection);
	}

	Billboard3D mesh;
	ShaderProgram coronaProgram;
	ldtex::Texture1D* Blackbody;
	uint64_t frame;
};


#endif // !CORONA_H
