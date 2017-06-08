#include "stdafx.h"
#include "engine\terrain\HeightNode.h"
#include "scenes\Terrain.h"

INITIALIZE_EASYLOGGINGPP
int main() {
	vulpes::terrain::Heightmap hm("rsrc/img/terrain_height.png");
	using namespace terrain_scene;
	TerrainScene scene;
	scene.RenderLoop();
	return 0;
}