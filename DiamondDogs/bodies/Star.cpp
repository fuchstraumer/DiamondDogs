#include "Star.h"
#include "glm/gtc/matrix_transform.hpp"

Texture1D Star::starColor = Texture1D("./rsrc/img/star/star_spectrum.png", 1024);
Texture2D Star::starTex = Texture2D("./rsrc/img/star/star_tex.png", 1024, 1024);

// Simple method to get a stars color based on its temperature
inline glm::vec3 getStarColor(unsigned int temperature) {
	return glm::vec3(temperature * (0.0534f / 255.0f) - (43.0f / 255.0f),
		temperature * (0.0628f / 255.0f) - (77.0f / 255.0f),
		temperature * (0.0735f / 255.0f) - (115.0f / 255.0f));
}

Star::Star(int lod_level, float _radius, unsigned int temp, const glm::mat4& projection) : corona() {
	radius = _radius;
	temperature = temp;
	mesh = Icosphere(lod_level, radius);
	// Time starts at 0
	frame = 0;
	// Setup shader program
	// Build subprograms
	Shader vert("./shaders/star/vertex.glsl", VERTEX_SHADER);
	Shader frag("./shaders/star/fragment.glsl", FRAGMENT_SHADER);
	shader.Init();
	shader.AttachShader(vert);
	shader.AttachShader(frag);
	// Complete and compile program
	shader.CompleteProgram();
	// Build map of uniform locations
	std::vector<std::string> uniforms{
		"model",
		"view",
		"projection",
		"normTransform",
		"radius",
		"colorShift",
		"temperature",
		"blackbody",
		"cameraPos",
		"frame",
		"aspect",
		"resolution",
	};
	shader.BuildUniformMap(uniforms);
	// Prepare mesh for rendering.
	shader.Use();
	mesh.BuildRenderData();
	// Set projection
	GLuint projLoc = shader.GetUniformLocation("projection");
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	GLuint radLoc = shader.GetUniformLocation("radius");
	glUniform1f(radLoc, radius);
	// Get the colorshift/radiosity increas based on the temp
	GLuint colorLoc = shader.GetUniformLocation("colorShift");
	glm::vec3 color = getStarColor(temperature);
	glUniform3f(colorLoc, color.x, color.y, color.z);
	// Get temperature location and set the parameter appropriately
	GLuint tempLoc = shader.GetUniformLocation("temperature");
	glUniform1i(tempLoc, temperature);
	GLuint starTexLoc = shader.GetUniformLocation("blackbody");
	glUniform1i(starTexLoc, 1);
	starColor.BuildTexture();
	GLuint aspectLoc = shader.GetUniformLocation("aspect");
	glUniform1f(aspectLoc, static_cast<GLfloat>(SCR_WIDTH) / static_cast<GLfloat>(SCR_HEIGHT));
	GLuint resLoc = shader.GetUniformLocation("resolution");
	glUniform2f(resLoc, static_cast<GLfloat>(SCR_WIDTH), static_cast<GLfloat>(SCR_HEIGHT));
}

void Star::BuildCorona(const glm::vec3& position, const float& radius, const glm::mat4& projection) {
	this->corona = Corona(position, radius, temperature);
}

void Star::Render(const glm::mat4 & view, const glm::mat4& projection){
	GLuint viewLoc = shader.GetUniformLocation("view");
	GLuint timeLoc = shader.GetUniformLocation("frame");
	shader.Use();
	glActiveTexture(GL_TEXTURE1);
	starColor.BindTexture();
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
	glUniform1i(timeLoc, frame);
	if (frame < std::numeric_limits<uint64_t>::max()) {
		frame++;
		this->mesh.Angle = glm::vec3(0.0f, 0.001f * frame, 0.0f);
		this->mesh.UpdateModelMatrix();
	}
	else {
		// Wrap time back to zero.
		frame = 0;
	}
	GLuint modelLoc = shader.GetUniformLocation("model");
	glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(this->mesh.Model));
	mesh.Render(shader);
	corona.mesh.Render(view,projection);
}
