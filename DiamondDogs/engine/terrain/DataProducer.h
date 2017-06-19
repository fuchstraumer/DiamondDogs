#pragma once
#ifndef VULPES_TERRAIN_DATA_PRODUCER_H
#define VULPES_TERRAIN_DATA_PRODUCER_H

#include "stdafx.h"

/*
	
	Defines a general-use data producer that acts as a worker object.
	Uses std::async to dispatch tasks given to it, and allows access
	to data when done

	Useful for offloading object generation from main thread.

	If flag of a terrain or height node is set to NEEDS_DATA, this
	object is still working on it. Once this flag changes to NEEDS_TRANFER
	or READY, the object will be retaken by the node renderer.

*/

#endif // !VULPES_TERRAIN_DATA_PRODUCER_H
