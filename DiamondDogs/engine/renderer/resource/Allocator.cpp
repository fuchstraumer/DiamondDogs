#include "stdafx.h"
#include "Allocator.h"

namespace vulpes {

	Allocation::Allocation(Allocator * alloc) : allocator(alloc), availSize(0), freeCount(0), Memory(VK_NULL_HANDLE), Size(0) {}

	void Allocation::Init(VkDeviceMemory & new_memory, const VkDeviceSize & new_size) {
		assert(Memory == VK_NULL_HANDLE);
		Memory = new_memory;
		Size = new_size;
		freeCount = 1;
		availSize = new_size;

		Suballocations.clear();
		availSuballocations.clear();

		// note/create suballocation defining our singular free region.
		Suballocation suballoc{ 0, new_size, SuballocationType::Free };
		Suballocations.push_back(std::move(suballoc));

		// add location of that suballocation to mapping vector
		auto suballoc_iter = Suballocations.end();
		availSuballocations.push_back(suballoc_iter);

	}

	ValidationCode Allocation::Validate() const {

		if (Memory == VK_NULL_HANDLE) {
			return ValidationCode::NULL_MEMORY_HANDLE;
		}

		if (Size == 0 || Suballocations.empty()) {
			return ValidationCode::ZERO_MEMORY_SIZE;
		}

		// expected offset of newest suballocation compared to previous
		VkDeviceSize calculated_offset = 0;
		// expected number of free suballocations, from traversing that list
		uint32_t calculated_free_suballocs = 0;
		// available size, taken as sum of avail size of all free suballocations.
		VkDeviceSize calculated_free_size = 0;
		// number of free suballocations that need to be registered in the list for these objects
		uint32_t num_suballocs_to_register = 0;
		// set as we iterate, true when prev suballoc was free (used to sort/swap entries)
		bool prev_entry_free = false;

		for (auto iter = Suballocations.cbegin(); iter != Suballocations.cend(); ++iter) {
			const Suballocation& curr = *iter;

			// if offset of current suballoc is wrong, then this allocation is invalid
			if (curr.offset != calculated_offset) {
				return ValidationCode::INCORRECT_SUBALLOC_OFFSET;
			}

			// two adjacent free entries is invalid: should be merged.
			const bool curr_free = (curr.type == SuballocationType::Free);
			if (curr_free && prev_entry_free) {
				return ValidationCode::NEED_MERGE_SUBALLOCS;
			}

			// update prev free status
			prev_entry_free = curr_free;

			if (curr_free) {
				calculated_free_size += curr.size;
				++calculated_free_suballocs;
				if (curr.size >= MinSuballocationSizeToRegister) {
					++num_suballocs_to_register; // this suballocation should be registered in the free list.
				}
			}

			calculated_offset += curr.size;
		}

		// Number of free suballocations in objects list doesn't match what our calculated value says we should have
		if (availSuballocations.size() != num_suballocs_to_register) {
			return ValidationCode::FREE_SUBALLOC_COUNT_MISMATCH;
		}

		VkDeviceSize last_entry_size = 0;
		for (size_t i = 0; i < availSuballocations.size(); ++i) {
			auto curr_iter = availSuballocations[i];

			// non-free suballoc in free list
			if (curr_iter->type != SuballocationType::Free) {
				return ValidationCode::USED_SUBALLOC_IN_FREE_LIST;
			}

			// sorting of free list is incorrect
			if (curr_iter->size < last_entry_size) {
				return ValidationCode::FREE_SUBALLOC_SORT_INCORRECT;
			}

			last_entry_size = curr_iter->size;
		}

		if (calculated_offset != Size) {
			return ValidationCode::FINAL_SIZE_MISMATCH;
		}

		if (calculated_free_size != availSize) {
			return ValidationCode::FINAL_FREE_SIZE_MISMATCH;
		}

		if (calculated_free_suballocs != freeCount) {
			return ValidationCode::FREE_SUBALLOC_COUNT_MISMATCH;
		}

		// shouldn't return this
		return ValidationCode::VALIDATION_PASSED;
	}

	bool Allocation::RequestSuballocation(const VkDeviceSize & buffer_image_granularity, const VkDeviceSize & allocation_size, const VkDeviceSize & allocation_alignment, SuballocationType allocation_type, SuballocationRequest * dest_request) {

		return false;
	}


}