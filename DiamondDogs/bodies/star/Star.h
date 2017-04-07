#pragma once
#ifndef STAR_H
#define STAR_H
#include "../Body.h"
#include "Corona.h"
#include "..\..\engine\mesh\Icosphere.h"
#include "..\..\engine\mesh\Billboard.h"

class Star : public Body {
public:
	// Creates a star, randomly selecting all values from within reasonable ranges
	Star(int lod_level, float _radius, unsigned int temp, const glm::mat4& projection);
	void BuildCorona(const glm::vec3 & position, const float & radius, const glm::mat4 & projection);
	// Creates a star, using supplied values or reasonably shuffled defaults otherwise.
	~Star() = default;
	Star() = default;
	Star& operator=(Star&& other);
	// Render this star, supplying the view matrix needed
	void Render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camera_position);
private:
	// Temperature selects color, and specifies which texture coordinate to use for all
	// vertices in this object since the base color is uniform
	unsigned int temperature;
	// Radius is useful to know, but will mainly be set in the mesh since we care about it most there
	float radius;
	// Meshes used for up-close rendering.
	Icosphere mesh;
	Icosphere mesh2;
	// Meshes used when star is far away.
	Billboard3D StarDistant;
	ShaderProgram shaderClose;
	// Shader used to render the sun when its distant.
	ShaderProgram shaderDistant;
	// Texture used to get color: blackbody radiation curve.
	ldtex::Texture1D* starColor;
	// Texture used to get texture (appearance of surface)
	ldtex::Texture2D* starTex;
	// Texture used to render star from a large distance.
	ldtex::Texture2D* starGlow;
	// Corona object for this star
	Corona corona;
	// Used to permute the star
	uint64_t frame;
	// Distance we switch LODs
	float LOD_SwitchDistance;
	
};

#endif // !STAR_H
