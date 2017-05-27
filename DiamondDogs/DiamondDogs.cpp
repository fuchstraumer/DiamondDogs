#include "stdafx.h"

#include "scenes\StarScene.h"

int main() {
	using namespace star_scene;
	enum class NodeFlag : uint8_t {
		Ready,
		Transfer,
		Prune,
	};

	std::unordered_multimap<NodeFlag, uint64_t*> node_map;

	return 0;
}