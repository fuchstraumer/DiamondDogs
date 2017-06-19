#include "stdafx.h"
#include "DataProducer.h"
#include "engine/renderer/core/LogicalDevice.h"

namespace vulpes {

	bool DataRequest::Complete() const {
		if (auto check = Result.lock()) {
			return true;
		}
		else {
			return false;
		}
	}

	Buffer* vulpes::DataRequest::GetData() const {
		if (!Complete()) {
			throw std::runtime_error("Tried to access data before it was generated.");
		}


	}

}
