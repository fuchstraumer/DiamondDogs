#pragma once
#ifndef STAR_H
#define STAR_H
#include "Body.h"
#include "../mesh/Icosphere.h"
#include "../mesh/Billboard.h"
// Structure defining a stars corona
struct Corona : public Billboard3D {
	Corona() : Billboard3D(coronaProgram) {

	}
	~Corona() = default;

	Corona(const glm::vec3& position, const float& radius, const glm::mat4& projection) : Billboard3D(coronaProgram) {
		coronaProgram.Init();
		Shader cVert("./shaders/billboard/corona_vertex.glsl", VERTEX_SHADER);
		Shader cFrag("./shaders/billboard/corona_fragment.glsl", FRAGMENT_SHADER);
		coronaProgram.AttachShader(cVert);
		coronaProgram.AttachShader(cFrag);
		coronaProgram.CompleteProgram();
		// Setup uniforms for billboard
		std::vector<std::string> uniforms{
			"model",
			"view",
			"projection",
			"viewTransform",
		};
		coronaProgram.BuildUniformMap(uniforms);
		Scale = glm::vec3(4 * radius);
		Position = position;
		Angle = glm::vec3(0.0f);
		BuildRenderData(projection);
	}

	ShaderProgram coronaProgram;
};
class Star : public Body {
public:
	// Creates a star, randomly selecting all values from within reasonable ranges
	Star(int lod_level, float _radius, unsigned int temp, const glm::mat4& projection);
	// Creates a star, using supplied values or reasonably shuffled defaults otherwise.

	~Star() = default;
	Star() = default;
	// Render this star, supplying the view matrix needed
	void Render(const glm::mat4& view, const glm::vec3& camera_position);
private:
	// Temperature selects color, and specifies which texture coordinate to use for all
	// vertices in this object since the base color is uniform
	unsigned int temperature;
	// Radius is useful to know, but will mainly be set in the mesh since we care about it most there
	float radius;
	Icosphere mesh;
	ShaderProgram shader;
	static Texture1D starColor;
	// Corona object for this star
	Corona corona;
};

#endif // !STAR_H
