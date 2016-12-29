#include "Star.h"
#include "glm/gtc/matrix_transform.hpp"
Texture1D Star::starColor = Texture1D("./rsrc/img/star/star_spectrum.png", 1024);

// Simple method to get a stars color based on its temperature
inline glm::vec3 getStarColor(unsigned int temperature) {
	return glm::vec3(temperature * (0.0534 / 255.0) - (43.0 / 255.0),
		temperature * (0.0628 / 255.0) - (77.0 / 255.0),
		temperature * (0.0735 / 255.0) - (115.0 / 255.0));
}

Star::Star(int lod_level, float _radius, unsigned int temp, const glm::mat4& projection) {
	radius = _radius;
	temperature = temp;
	mesh = Icosphere(lod_level, radius);
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
	starColor.BuildTexture();
}

void Star::Render(const glm::mat4 & view){
	GLuint viewLoc = shader.GetUniformLocation("view");
	shader.Use();
	glActiveTexture(GL_TEXTURE1);
	starColor.BindTexture();
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
	mesh.Render(shader);
}
