#include "stdafx.h"
#include "scenes\Terrain.h"
#include "scenes\StarScene.h"
#include "engine/util/WorkerThread.hpp"
#include "engine/subsystems/terrain/HeightNode.hpp"
INITIALIZE_EASYLOGGINGPP



int main() {

	el::Configurations conf("logging.ini");
	el::Loggers::reconfigureAllLoggers(conf);

	auto wait_test_func = []() {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		std::cout << "Waited for 250ms...";
	};

	terrain_scene::TerrainScene scene;
	scene.RenderLoop();

	return 0;
}