#include "stdafx.h"
#include "Star.h"
#include "glm/gtc/matrix_transform.hpp"

Texture1D Star::starColor = Texture1D("./rsrc/img/star/star_spectrum.png", 1024);
Texture2D Star::starTex = Texture2D("./rsrc/img/star/star_tex.png", 1024, 1024);
Texture2D Star::starGlow = Texture2D("./rsrc/img/star/star_glow.png", 2048, 2048);

// Simple method to get a stars color based on its temperature
inline glm::vec3 getStarColor(unsigned int temperature) {
	return glm::vec3(temperature * (0.0534f / 255.0f) - (43.0f / 255.0f),
		temperature * (0.0628f / 255.0f) - (77.0f / 255.0f),
		temperature * (0.0735f / 255.0f) - (115.0f / 255.0f));
}

Star::Star(int lod_level, float _radius, unsigned int temp, const glm::mat4& projection) : corona() {
	radius = _radius;
	temperature = temp;
	LOD_SwitchDistance = radius * 3000.0f;
	// Setup primary meshes.
	mesh = Icosphere(lod_level, radius);
	mesh2 = Icosphere(lod_level + 1, radius * 1.02f);
	mesh2.Angle = glm::vec3(30.0f, 45.0f, 90.0f);
	// Setup long-distance billboard.

	// Time starts at 0
	frame = 0;
	// Setup shader program for rendering star when close.

	// Build program elements
	Shader vert("./shaders/star/close/vertex.glsl", VERTEX_SHADER);
	Shader frag("./shaders/star/close/fragment.glsl", FRAGMENT_SHADER);
	shaderClose.Init();
	shaderClose.AttachShader(vert);
	shaderClose.AttachShader(frag);

	// Complete and compile program
	shaderClose.CompleteProgram();

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
		"opacity",
	};
	shaderClose.BuildUniformMap(uniforms);

	// Prepare mesh for rendering.
	shaderClose.Use();
	mesh.BuildRenderData();
	mesh2.BuildRenderData();

	// Set projection matrix
	GLuint projLoc = shaderClose.GetUniformLocation("projection");
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

	// Set radius
	GLuint radLoc = shaderClose.GetUniformLocation("radius");
	glUniform1f(radLoc, radius);

	// Get the colorshift/radiosity increase based on the temp
	GLuint colorLoc = shaderClose.GetUniformLocation("colorShift");
	glm::vec3 color = getStarColor(temperature);
	glUniform3f(colorLoc, color.x, color.y, color.z);

	// Get temperature location and set the parameter appropriately
	GLuint tempLoc = shaderClose.GetUniformLocation("temperature");
	glUniform1i(tempLoc, temperature);

	// Set texture location and build the texture data
	GLuint starTexLoc = shaderClose.GetUniformLocation("blackbody");
	glUniform1i(starTexLoc, 1);
	starColor.BuildTexture();

	// Setup the billboard used when we want to render the star from a long distance.

	// First, setup the program used to render at this distance.
	Shader farVert("./shaders/star/far/vertex.glsl", VERTEX_SHADER);
	Shader farFrag("./shaders/star/far/fragment.glsl", FRAGMENT_SHADER);
	shaderDistant.Init();
	shaderDistant.AttachShader(farVert);
	shaderDistant.AttachShader(farFrag);
	shaderDistant.CompleteProgram();

	// Setup uniforms for this program.
	uniforms = std::vector<std::string>{
		"view",
		"projection",
		"model",
		"normTransform",
		"center",
		"size", // Size of corona in world-space units.
		"cameraUp",
		"cameraRight",
		"temperature",
		"glowTex",
	};
	shaderDistant.BuildUniformMap(uniforms);

	// Make sure to build the texture and set it's location
	starGlow.BuildTexture();

	projLoc = shaderDistant.GetUniformLocation("projection");
	tempLoc = shaderDistant.GetUniformLocation("temperature");

}

void Star::BuildCorona(const glm::vec3& position, const float& radius, const glm::mat4& projection) {
	this->corona = Corona(position, radius);
	corona.BuildRenderData(temperature);
}

void Star::Render(const glm::mat4 & view, const glm::mat4& projection, const glm::vec3& camera_position){
	if (glm::distance(camera_position, WorldPosition) <= LOD_SwitchDistance) {
		GLuint viewLoc = shaderClose.GetUniformLocation("view");
		GLuint timeLoc = shaderClose.GetUniformLocation("frame");
		GLuint cPosLoc = shaderClose.GetUniformLocation("cameraPos");
		GLuint opacityLoc = shaderClose.GetUniformLocation("opacity");
		shaderClose.Use();
		glActiveTexture(GL_TEXTURE1);
		starColor.BindTexture();
		glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
		glUniform1i(timeLoc, frame);
		glUniform3f(cPosLoc, camera_position.x, camera_position.y, camera_position.z);
		if (frame < std::numeric_limits<uint64_t>::max()) {
			frame++;
			this->mesh.Angle = glm::vec3(0.0f, 0.01f * frame, 0.0f);
			this->mesh2.Angle = glm::vec3(-0.01f * frame, 0.0f, 0.02f * frame);
			this->mesh2.UpdateModelMatrix();
			this->mesh.UpdateModelMatrix();
		}
		else {
			// Wrap time back to zero.
			frame = 0;
		}
		glUniform1f(opacityLoc, 1.0f);
		mesh.Render(shaderClose);
		glUniform1f(opacityLoc, 0.6f);
		mesh2.Render(shaderClose);
		corona.Render(view, projection);
	}
	else {
		// Calculate size of the glow ("far") billboard
		// Constants based on our actual sun, Sol
		constexpr double dSun = 1382684.0;
		constexpr double tSun = 5778.0;
		auto glowSize = [dSun, tSun](float diameter, float temp, double distance)->float {
			double D = diameter * dSun;
			double L = (D*D) * pow(temp / tSun, 4.0);
			return static_cast<float>(0.016 * pow(L, 0.25) / pow(distance, 0.50));
		};
		// note: shouldn't constexpr this in case we change it later.
		float aspectRatio = static_cast<GLfloat>(SCR_WIDTH) / static_cast<GLfloat>(SCR_HEIGHT);
		// Set the size in the shader.
		glm::vec2 size = glm::vec2(glowSize(radius * 2.0, temperature, glm::distance(camera_position, WorldPosition)));
		size.y *= aspectRatio;
		GLuint sizeLoc = shaderDistant.GetUniformLocation("size");
		glUniform2f(sizeLoc, size.x, size.y);
		glActiveTexture(GL_TEXTURE5);
		starGlow.BindTexture();
	}
}
