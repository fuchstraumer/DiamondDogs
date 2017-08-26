#pragma once
#ifndef CORONA_H
#define CORONA_H
#include "stdafx.h"
#include "engine/objects/mesh/Billboard.h"



	class Corona : public Billboard {
	public:

		Corona(const vulpes::Device* dvc, const float& radius, const glm::vec3& position = glm::vec3(0.0f));

	protected:

		virtual void createShaders() override;

	};



#endif // !CORONA_H
