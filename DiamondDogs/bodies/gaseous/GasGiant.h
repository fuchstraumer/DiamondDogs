#pragma once
#ifndef GAS_GIANT_H
#define GAS_GIANT_H
#include "../Body.h"
#include "../../mesh/SpherifiedCube.h"
#include "../../util/lodeTexture.h"
/*
	
	Gas Giant:

	Class for rendering large gas-giant objects. Uses the spherified
	cubemap mesh since we like grids (compute shader). Uses a compute
	shader to generate a huge procedural texture that is then used to 
	generate realistic-looking cloud bands.

*/

class GasGiant : public Body {

	GasGiant(double radius, double mass, unsigned int subdivisions) {
		// Build textures by allocating space for them.
		for (auto tex : faceTextures) {
			tex = EmptyTexture2D(subdivisions * 2, subdivisions * 2);
			tex.BuildTexture();
		}
	}

private:

	// Prepares the compute shader by setting up the workgroups and destination textures
	// for each of the six faces.
	void initParticles();

	// Dispatches and runs compute shaders for the whole mesh
	// by iterating through each cube face.
	void runComputeShaders();

	struct particle {

		particle(glm::vec3 pos = glm::vec3(0.0f), glm::vec4 color = glm::vec4(1.0f)) : Position(pos), Color(color) {}

		~particle() = default;

		glm::vec3 Position;
		glm::vec4 Color;

		static float AlphaBlendColor(const float& underchannel, const float& underalpha, const float& overchannel, const float& overalpha);

	};

	std::array<EmptyTexture2D, 6> faceTextures;
};


#endif // !GAS_GIANT_H
