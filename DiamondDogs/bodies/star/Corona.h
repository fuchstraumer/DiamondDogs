#pragma once
#ifndef CORONA_H
#define CORONA_H
#include "stdafx.h"

#include "engine/objects/Billboard.h"
#include "engine\renderer\objects\texture.h"
#include "engine\renderer\objects\shader_object.h"
#include "engine\renderer\objects\pipeline_object.h"

// Structure defining a stars corona
struct Corona {

	Corona(const Corona& other) = delete;
	Corona& operator=(const Corona& other) = delete;

	Corona(Corona&& other) : Blackbody(std::move(other.Blackbody)), Program(std::move(other.Program)), mesh(std::move(other.mesh)), frame(std::move(other.frame)) {}

	Corona& operator=(Corona&& other) {
		Blackbody = std::move(other.Blackbody);
		Program = std::move(other.Program);
		mesh = std::move(other.mesh);
		frame = std::move(other.frame);
		return *this;
	}

	Corona(const float& radius, const glm::vec3& position = glm::vec3(0.0f)) : mesh(radius, position), Blackbody("./rsrc/img/star/star_spectrum.png", 1024) {
		vulpes::shader_object<vulpes::vertex_shader_t> cVert("./shaders/billboard/corona_vertex.glsl");
		vulpes::shader_object<vulpes::fragment_shader_t> cFrag("./shaders/billboard/corona_fragment.glsl");
		glUseProgramStages(Program.handles[0], GL_VERTEX_SHADER_BIT, cVert.ID);
		glUseProgramStages(Program.handles[0], GL_FRAGMENT_SHADER_BIT, cFrag.ID);
		Program.setup_uniforms();
	}

	void BuildRenderData(const int& star_temperature) {
		// Set frame counter to zero
		frame = 0;
		GLuint tempLoc = Program.uniforms.at("temperature");
		glProgramUniform1i(Program.handles[0], tempLoc, star_temperature);
		GLuint texLoc = Program.uniforms.at("blackbody");
		glProgramUniform1i(Program.handles[0], texLoc, 0);
		mesh.Program = std::move(Program);
		mesh.BuildRenderData();
	}

	void Render(const glm::mat4 & view, const glm::mat4& projection) {
		glBindTextureUnit(3, Blackbody.handles[0]);
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
		GLuint frameLoc = mesh.Program.uniforms.at("frame");
		glProgramUniform1i(mesh.Program.handles[0], frameLoc, static_cast<GLint>(frame));
		mesh.Render(view, projection);
	}

	Billboard3D mesh;
	vulpes::program_pipeline_object Program;
	vulpes::texture_1d Blackbody;
	uint64_t frame;
};


#endif // !CORONA_H
