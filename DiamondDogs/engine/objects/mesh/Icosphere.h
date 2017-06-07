#pragma once
#ifndef ICOSPHERE_H
#define ICOSPHERE_H
#include "Mesh.h"
/*
	ICOSPHERE_H

	Simple mesh sub-class that generates an icosphere mesh. This is an isocahedral
	mesh that is subdivided to a maximum level given during initialization of this
	object.

	Best used for objects that don't require the benefits of cubemapped meshes, but still
	benefit from highly uniform spherical meshes.

*/
namespace vulpes {
	
	namespace mesh {

		class Icosphere : public Mesh {
		public:

			Icosphere(Icosphere&& other) noexcept;
			Icosphere& operator=(Icosphere&& other) noexcept;

			Icosphere(unsigned int lod_level, float radius = 1.0f, glm::vec3 position = glm::vec3(0.0f), glm::vec3 rotation = glm::vec3(0.0f));

			unsigned int LOD_Level;

			~Icosphere() = default;
			Icosphere() = default;
		};

	}
}
#endif // !ICOSPHERE_H
