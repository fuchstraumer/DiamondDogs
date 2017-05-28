#pragma once
#ifndef COMMON_DEFINES_H
#define COMMON_DEFINES_H

namespace vulpes {

	namespace terrain {
		enum class NodeStatus {
			Undefined, // Likely not constructed fully or used at all
			OutOfFrustum,
			OutOfRange,
			Active, // Being used in next frame
			Subdivided, // Has been subdivided, render children instead of this.
			NeedsTransfer, // needs mesh built and transferred to device, and status changed to active then
		};
	}
}


#endif // !COMMON_DEFINES_H
