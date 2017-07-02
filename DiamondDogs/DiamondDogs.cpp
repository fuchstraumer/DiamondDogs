#include "stdafx.h"
#include "engine\terrain\HeightNode.h"
#include "scenes\Terrain.h"
#include "scenes\ComputeTests.h"

INITIALIZE_EASYLOGGINGPP
int main() {

	using namespace vulpes::terrain;
	using namespace terrain_scene;
	TerrainScene scene;
	scene.RenderLoop();
	return 0;
}