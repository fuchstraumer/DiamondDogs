#include "stdafx.h"
#include "scenes\Terrain.h"
INITIALIZE_EASYLOGGINGPP
int main() {
	using namespace terrain_scene;
	TerrainScene scene;
	scene.RenderLoop();
	return 0;
}