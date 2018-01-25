#include "stdafx.h"
#include "Corona.hpp"

using namespace vulpes;


	Corona::Corona(const vulpes::Device * dvc, const float & radius, const glm::vec3 & position) : Billboard(dvc, radius, position) {}

	void Corona::createShaders() {

		vert = std::make_unique<ShaderModule>(device, "rsrc/shaders/billboard/corona.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		frag = std::make_unique<ShaderModule>(device, "rsrc/shaders/billboard/corona.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

	}

