#pragma once
#ifndef STAR_DEMO_H
#define STAR_DEMO_H

#include "stdafx.h"
#include "../engine/Context.h"
#include "bodies\star\Star.h"
#include "engine\mesh\Skybox.h"

namespace demo {
	static const std::vector<std::string> skyboxTextures{
		"./rsrc/img/skybox/right.png",
		"./rsrc/img/skybox/left.png",
		"./rsrc/img/skybox/top.png",
		"./rsrc/img/skybox/bottom.png",
		"./rsrc/img/skybox/front.png",
		"./rsrc/img/skybox/back.png",
	};

	class StarDemo : public Context {
	public:

		StarDemo();

		virtual void Render() override;

		Star demoStar;
		Skybox demoSkybox;
	};
}

#endif // !STAR_DEMO_H
