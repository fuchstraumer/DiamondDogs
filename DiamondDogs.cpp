#include "stdafx.h"
#include "engine\terrain\HeightNode.h"
#include "scenes\Terrain.h"
#include "scenes\StarScene.h"
INITIALIZE_EASYLOGGINGPP
int main() {
	using namespace star_scene;
	StarScene scene;
	scene.RenderLoop();
	/*using namespace vulpes::terrain;
	using namespace terrain_scene;
	TerrainScene scene;
	scene.RenderLoop();*/
	return 0;
}