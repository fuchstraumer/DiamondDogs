#pragma once
#ifndef TERRESTRIAL_H
#define TERRESTRIAL_H
#include "Body.h"
#include "../mesh/GlobeMesh.h"
#include "../util/Shader.h"
#include "../util/noise/Noise.h"

class Terrestrial : public Body {
public:
	Terrestrial() = default;
	~Terrestrial() = default;

	Terrestrial(float radius, double mass, int LOD, float atmo_density = 1.0f);

	void SetDiffuseColor(const glm::vec3& color);

	glm::vec3 GetDiffuseColor() const;

	float GetAtmoRadius() const;

	void SetAtmoRadius(const float& rad);

	float GetAtmoDensity() const;

	void SetAtmoDensity(const float& density);

	void SetAtmoColor(const glm::vec4& new_spectrum);

	glm::vec4 GetAtmoColor() const;

	// TODO: add params
	void BuildTerrain();

	void Render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos);

private:
	// Controls radius of the atmosphere
	float atmoRadius;
	// Controls density of the atmosphere.
	float atmoDensity;
	// Controls the refraction spectrum of this atmosphere, or its color
	glm::vec4 atmoSpectrum;
	// Controls the diffuse color of the surface of the object
	glm::vec4 surfaceDiffuse;
	// Mesh used to generate the atmosphere
	GlobeMesh atmosphere;
	// Shader for the atmosphere alone
	ShaderProgram atmoShader;
	// Model matrix for this object
	glm::mat4 model;
	// Scale matrix
	glm::mat4 scale;
	// Translation matrix
	glm::mat4 translation;
	// Mesh for this object
	SpherifiedCube Mesh;
	// Main shader for drawing the surface of this object
	ShaderProgram MainShader;
	// Terrain generator
	NoiseGenerator terrainGen;
};
#endif // !TERRESTRIAL_H
