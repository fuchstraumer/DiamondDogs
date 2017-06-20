#include "stdafx.h"
#include "engine\terrain\HeightNode.h"
#include "scenes\Terrain.h"
#include "scenes\ComputeTests.h"

INITIALIZE_EASYLOGGINGPP
int main() {

	using namespace compute_tests;
	ComputeTests tests;
	tests.ExecuteTests(4);

	using namespace vulpes::terrain;
	using namespace terrain_scene;
	TerrainScene scene;
	scene.RenderLoop();
	return 0;
}