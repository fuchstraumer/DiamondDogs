#pragma once
#ifndef CORONA_H
#define CORONA_H
#include "stdafx.h"
#include "../../util/Shader.h"
#include "../../mesh/Billboard.h"
#include "../../util/lodeTexture.h"

// Structure defining a stars corona
struct Corona {

	Corona() : Blackbody("./rsrc/img/star/star_spectrum.png", 1024) {}

	~Corona() = default;

	Corona(const glm::vec3& position, const float& radius) : Blackbody("./rsrc/img/star/star_spectrum.png", 1024) {
		coronaProgram = std::make_shared<ShaderProgram>();
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
		mesh.Render(view, projection);
	}

	Billboard3D mesh;
	std::shared_ptr<ShaderProgram> coronaProgram;
	Texture1D Blackbody;
};


#endif // !CORONA_H
