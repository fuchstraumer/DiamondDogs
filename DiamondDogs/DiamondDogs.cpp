#include "stdafx.h"

#include "scenes\AtmoScene.h"

#include "engine\terrain\TerrainQuadtree.h"

int main() {
	using namespace vulpes::terrain;
	TerrainQuadtree tree(100.0f, 15, 3000.0, glm::vec3(0.0f));
	return 0;
}