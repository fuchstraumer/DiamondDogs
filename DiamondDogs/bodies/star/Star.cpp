#include "stdafx.h"
#include "Star.h"

// Simple method to get a stars color based on its temperature
inline glm::vec3 getStarColor(unsigned int temperature) {
	return glm::vec3(temperature * (0.0534f / 255.0f) - (43.0f / 255.0f),
		temperature * (0.0628f / 255.0f) - (77.0f / 255.0f),
		temperature * (0.0735f / 255.0f) - (115.0f / 255.0f));
}

Star::Star(int lod_level, float _radius, unsigned int temp, const glm::mat4& projection, const glm::vec3& position) : corona(_radius * 6.0f, position), StarDistant(_radius, position) {
	radius = _radius;
	temperature = temp;
	LOD_SwitchDistance = radius * 10.0f;
	// Setup primary meshes.
	mesh = Icosphere(lod_level, radius);
	mesh2 = Icosphere(lod_level, radius * 1.0025f);
	mesh2.Angle = glm::vec3(30.0f, 45.0f, 90.0f);
	
	starColor = new ldtex::Texture1D("./rsrc/img/star/star_spectrum.png", 1024);
	starTex = new ldtex::Texture2D("./rsrc/img/star/star_tex.png", 1024, 1024);
	starGlow = new ldtex::Texture2D("./rsrc/img/star/star_glow.png", 2048, 2048);

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
	starColor->BuildTexture();

	// Setup the billboard used when we want to render the star from a long distance.

	// First, setup the program used to render at this distance.
	Shader farVert("./shaders/star/far/vertex.glsl", VERTEX_SHADER);
	Shader farFrag("./shaders/star/far/fragment.glsl", FRAGMENT_SHADER);
	StarDistant.Program.Init();
	StarDistant.Program.AttachShader(farVert);
	StarDistant.Program.AttachShader(farFrag);
	StarDistant.Program.CompleteProgram();

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
	StarDistant.Program.BuildUniformMap(uniforms);
	// Make sure to build the texture and set it's location
	starGlow->BuildTexture();
	StarDistant.Program.Use();

	// Build render data.
	StarDistant.BuildRenderData();

	// Get locations of uniforms we can set right now.
	projLoc = StarDistant.Program.GetUniformLocation("projection");
	tempLoc = StarDistant.Program.GetUniformLocation("temperature");
	starTexLoc = StarDistant.Program.GetUniformLocation("glowTex");
	GLuint centerLoc = StarDistant.Program.GetUniformLocation("center");

	// Set uniforms
	glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
	glUniform1i(tempLoc, temperature);
	glUniform1i(starTexLoc, 5);
	glUniform3f(centerLoc, StarDistant.Position.x, StarDistant.Position.y, StarDistant.Position.z);

	// Finish setting up the corona 
	corona.BuildRenderData(temperature);

}

Star& Star::operator=(Star && other){
	temperature = std::move(other.temperature);
	radius = std::move(other.radius);
	mesh = std::move(other.mesh);
	mesh2 = std::move(other.mesh2);
	StarDistant = std::move(other.StarDistant);
	starColor = std::move(other.starColor);
	starTex = std::move(other.starTex);
	starGlow = std::move(other.starGlow);
	corona = std::move(other.corona);
	frame = std::move(other.frame);
	LOD_SwitchDistance = std::move(other.LOD_SwitchDistance);
	return *this;
}

void Star::Render(const glm::mat4 & view, const glm::mat4& projection, const glm::vec3& camera_position){
	GLuint viewLoc = shaderClose.GetUniformLocation("view");
	GLuint timeLoc = shaderClose.GetUniformLocation("frame");
	GLuint cPosLoc = shaderClose.GetUniformLocation("cameraPos");
	GLuint opacityLoc = shaderClose.GetUniformLocation("opacity");
	shaderClose.Use();
	glActiveTexture(GL_TEXTURE1);
	starColor->BindTexture();
	glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
	glUniform1i(timeLoc, static_cast<GLint>(frame));
	glUniform3f(cPosLoc, camera_position.x, camera_position.y, camera_position.z);
	if (frame < std::numeric_limits<GLint>::max()) {
		frame++;
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
