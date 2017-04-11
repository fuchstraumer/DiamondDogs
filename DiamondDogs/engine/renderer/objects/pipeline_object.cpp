#include "stdafx.h"
#include "pipeline_object.h"

vulpes::program_pipeline_object::program_pipeline_object() : device_object() {}

vulpes::program_pipeline_object::~program_pipeline_object() {
	glDeleteProgram(program_id);
}

vulpes::program_pipeline_object::program_pipeline_object(program_pipeline_object && other) noexcept : program_id(std::move(other.program_id)), uniforms(std::move(other.uniforms)) {
	other.program_id = 0; 
}

vulpes::program_pipeline_object& vulpes::program_pipeline_object::operator=(program_pipeline_object && other) noexcept {
	program_id = std::move(other.program_id);
	uniforms = std::move(other.uniforms);
	other.program_id = 0;
	return *this;
}

void vulpes::program_pipeline_object::attach_program(GLuint _program_id, GLbitfield stages) {
	glUseProgramStages(this->handles[0], stages, program_id);
	program_id = _program_id;
}
const GLuint vulpes::program_pipeline_object::at(const std::string & uniform_name) const {
	return uniforms.at(uniform_name);
}

void vulpes::program_pipeline_object::setup_uniforms() {
	GLint uniform_count = 0;
	GLsizei length = 0, size = 0;
	GLenum type = GL_NONE;
	glGetProgramInterfaceiv(program_id, GL_UNIFORM, GL_ACTIVE_RESOURCES, &uniform_count);
	for (GLint i = 0; i < uniform_count; i++) {
		std::array<GLchar, 0xff> uniform_name = {};
		glGetActiveUniform(program_id, i, uniform_name.size(), &length, &size, &type, uniform_name.data());
		uniforms[uniform_name.data()] = glGetUniformLocation(program_id, uniform_name.data());
	}
}
