#pragma once
#ifndef VULPES_TERRAIN_SAMPLER_H

#include "stdafx.h"

namespace vulpes {

	namespace terrain {

		/*
		
			class - TerrainSampler

			Stores data (of various types) for sampling when creating terrain nodes. 
			This class is abstract: TerrainHeightSampler is the first concrete implementation
			of this class. 

			Other implementations should come about for sampling things like colors, environmental data,
			atmospheric data, and more. 
		
		*/

	}

}

#endif // !VULPES_TERRAIN_SAMPLER_H
