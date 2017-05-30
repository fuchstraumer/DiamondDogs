#ifndef VULPES_VK_CMD_BUFFER_SUBMITTER_H
#define VULPES_VK_CMD_BUFFER_SUBMITTER_H

#include "stdafx.h"
#include "../ForwardDecl.h"

namespace vulpes {

	struct SubmitObserver {
		virtual void notify() const = 0;
	};

    class CommandSubmitter {
    public:

		CommandSubmitter(const Device* _parent);

		void AddObserver(const SubmitObserver* observer);

	protected:
		VkQueue queue;
		VkFence fence;
		const Device* parent;
		// notifies all of these when submission completes.
		std::vector<const SubmitObserver*> observers;
		// command buffers to submit
		std::vector<std::reference_wrapper<VkCommandBuffer>> cmdBuffers;
    };

}

#endif //!VULPES_VK_CMD_BUFFER_SUBMITTER_H