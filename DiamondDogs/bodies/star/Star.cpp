#include "stdafx.h"
#include "Star.h"
#include "../../engine/renderer/util/glsl_compiler.h"
// Simple method to get a stars color based on its temperature
inline glm::vec3 getStarColor(unsigned int temperature) {
	return glm::vec3(temperature * (0.0534f / 255.0f) - (43.0f / 255.0f),
		temperature * (0.0628f / 255.0f) - (77.0f / 255.0f),
		temperature * (0.0735f / 255.0f) - (115.0f / 255.0f));
}

Star::Star(int lod_level, float _radius, unsigned int temp, const glm::mat4& projection, const glm::vec3& position) : corona(_radius * 6.0f, position), star_color("./rsrc/img/star/star_spectrum.png", 1024), mesh(lod_level, radius), mesh2(lod_level, radius * 1.0025f) {
	radius = _radius;
	temperature = temp;
	LOD_SwitchDistance = radius * 10.0f;
	// Setup primary meshes.
	mesh2.angle = glm::vec3(30.0f, 45.0f, 90.0f);

	// Time starts at 0
	frame = 0;
	// Setup shader program for rendering star

	vulpes::compiler cl(vulpes::profile::CORE, 450);
	cl.add_shader<vulpes::vertex_shader_t>("./shaders/star/close/vertex.glsl");
	cl.add_shader<vulpes::fragment_shader_t>("./shaders/star/close/fragment.glsl");
	GLuint program_id = cl.link();
	GLbitfield program_stages = cl.get_program_stages();

	pipeline.attach_program(program_id, program_stages);
	pipeline.setup_uniforms();

	glUseProgram(program_id);
	mesh.build_render_data(pipeline, projection);
	mesh2.build_render_data(pipeline, projection);

	// Set projection matrix
	glProgramUniformMatrix4fv(program_id, pipeline.at("projection"), 1, GL_FALSE, glm::value_ptr(projection));

	// Get the colorshift/radiosity increase based on the temp
	glm::vec3 color = getStarColor(temperature);
	glProgramUniform3f(program_id, pipeline.at("colorShift"), color.x, color.y, color.z);

	// Get temperature location and set the parameter appropriately
	glProgramUniform1i(program_id, pipeline.at("temperature"), temperature);

	// Finish setting up the corona 
	corona.BuildRenderData(temperature, projection);

}

Star& Star::operator=(Star && other){
	temperature = std::move(other.temperature);
	radius = std::move(other.radius);
	mesh = std::move(other.mesh);
	mesh2 = std::move(other.mesh2);
	star_color = std::move(other.star_color);
	corona = std::move(other.corona);
	frame = std::move(other.frame);
	LOD_SwitchDistance = std::move(other.LOD_SwitchDistance);
	return *this;
}

void Star::Render(const glm::mat4 & view, const glm::mat4& projection, const glm::vec3& camera_position){
	glUseProgram(pipeline.program_id);
	glBindTextureUnit(1, star_color.handles[0]);
	glProgramUniform1i(pipeline.program_id, pipeline.at("blackbody"), 1);
	glProgramUniformMatrix4fv(pipeline.program_id, pipeline.at("view"), 1, GL_FALSE, glm::value_ptr(view));
	glProgramUniform1i(pipeline.program_id, pipeline.at("frame"), static_cast<GLint>(frame));
	glProgramUniform3f(pipeline.program_id, pipeline.at("cameraPos"), camera_position.x, camera_position.y, camera_position.z);
	if (frame < std::numeric_limits<GLint>::max()) {
		frame++;
	}
	else {
		// Wrap time back to zero.
		frame = 0;
	}
	glProgramUniform1f(pipeline.program_id, pipeline.at("opacity"), 1.0f);
	mesh.render(pipeline, view);
	glProgramUniform1f(pipeline.program_id, pipeline.at("opacity"), 0.6f);
	mesh2.render(pipeline, view);
	glUseProgram(0);
	corona.Render(view);
}
