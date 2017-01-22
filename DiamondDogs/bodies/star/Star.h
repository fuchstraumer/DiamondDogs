#pragma once
#ifndef STAR_H
#define STAR_H
#include "../Body.h"
#include "../../mesh/Icosphere.h"
#include "../../mesh/Billboard.h"

// Structure defining a stars corona
struct Corona {
	Corona() {}
	~Corona() = default;

	Corona(const glm::vec3& position, const float& radius, const float& star_temp) {
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
			"center",
			"size", // Size of corona in world-space units.
			"cameraUp",
			"cameraRight",
			"frame",
			"temperature",
		};
		coronaProgram->BuildUniformMap(uniforms);
		mesh.Program = coronaProgram;
		mesh.Scale = glm::vec3(4 * radius);
		mesh.Radius = 4.0f * radius;
		mesh.Position = position;
		mesh.Angle = glm::vec3(0.0f);
		mesh.BuildRenderData(star_temp);
	}
	
	Billboard3D mesh;
	std::shared_ptr<ShaderProgram> coronaProgram;
};

// Structure defining the little particle fountains around a star
struct Particles {

	Particles(uint num_of_particles) {
		posInit.resize(num_of_particles);
		// Size to allocate for
		GLuint bufferSize = num_of_particles * sizeof(GLfloat) * 3;
		glGenBuffers(1, &SBO);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, SBO);
		glBufferData(GL_SHADER_STORAGE_BUFFER, bufferSize, &posInit[0], GL_DYNAMIC_DRAW);
		Program = std::make_shared<ShaderProgram>();
		Shader pVert("./shaders/particles/vertex.glsl", VERTEX_SHADER);
		Shader pFrag("./shaders/particles/fragment.glsl", FRAGMENT_SHADER);
		Shader pCompute("./shaders/particles/compute.glsl", COMPUTE_SHADER);
		Program->Init();
		Program->AttachShader(pVert);
		Program->AttachShader(pFrag);
		Program->AttachShader(pCompute);
		Program->CompleteProgram();
		std::vector<std::string> uniforms{
			"model",
			"view",
			"projection",
		};
		Program->BuildUniformMap(uniforms);
	}

	// Buffer of initial particle positions.
	std::vector<glm::vec3> posInit;
	// Binding to storage buffer
	GLuint SBO;
	// Shader Program for the particle computation and visualization
	// shared_ptr for default issues, and for safe access from parent object
	std::shared_ptr<ShaderProgram> Program;
};

class Star : public Body {
public:
	// Creates a star, randomly selecting all values from within reasonable ranges
	Star(int lod_level, float _radius, unsigned int temp, const glm::mat4& projection);
	void BuildCorona(const glm::vec3 & position, const float & radius, const glm::mat4 & projection);
	// Creates a star, using supplied values or reasonably shuffled defaults otherwise.

	~Star() = default;
	Star() = default;
	// Render this star, supplying the view matrix needed
	void Render(const glm::mat4& view, const glm::mat4& projection);
private:
	// Temperature selects color, and specifies which texture coordinate to use for all
	// vertices in this object since the base color is uniform
	unsigned int temperature;
	// Radius is useful to know, but will mainly be set in the mesh since we care about it most there
	float radius;
	Icosphere mesh;
	ShaderProgram shader;
	// Texture used to get color: blackbody radiation curve.
	static Texture1D starColor;
	// Texture used to get texture (appearance of surface)
	static Texture2D starTex;
	// Corona object for this star
	Corona corona;
	// Used to permute the star
	uint64_t frame;
	
};

#endif // !STAR_H
