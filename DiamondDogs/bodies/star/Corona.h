#pragma once
#ifndef CORONA_H
#define CORONA_H
#include "stdafx.h"

#include "engine/objects/Billboard.h"
#include "engine\renderer\objects\texture.h"
#include "engine\renderer\util\glsl_compiler.h"
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
		vulpes::compiler cl(vulpes::profile::CORE, 450);
		cl.add_shader<vulpes::vertex_shader_t>("./shaders/billboard/corona_vertex.glsl");
		cl.add_shader<vulpes::fragment_shader_t>("./shaders/billboard/corona_fragment.glsl");
		GLuint program = cl.link();
		GLbitfield stages = cl.get_program_stages();
		glUseProgramStages(Program.handles[0], stages, program);
		Program.setup_uniforms(program);
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
