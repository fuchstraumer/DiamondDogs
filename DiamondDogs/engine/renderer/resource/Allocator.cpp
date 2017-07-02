#include "stdafx.h"
#include "Allocator.h"
#include "engine/util/easylogging++.h"
#include "../core/LogicalDevice.h"
#include "../core/PhysicalDevice.h"

namespace vulpes {

	VkBool32 AllocationRequirements::noNewAllocations = false;

	// padding to inject at end of allocations ot test allocation system
	static constexpr size_t DEBUG_PADDING = 0;

	Allocation::Allocation(Allocator * alloc) : allocator(alloc), availSize(0), freeCount(0), memory(VK_NULL_HANDLE), Size(0) {}

	Allocation::~Allocation() {
		assert(memory == VK_NULL_HANDLE);
	}

	void Allocation::Init(VkDeviceMemory & new_memory, const VkDeviceSize & new_size) {
		assert(memory == VK_NULL_HANDLE);
		memory = new_memory;
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

	void Allocation::Destroy(Allocator * alloc) {
		vkFreeMemory(alloc->DeviceHandle(), memory, nullptr);
		memory = VK_NULL_HANDLE;
	}

	bool Allocation::operator<(const Allocation & other) {
		return (availSize < other.availSize);
	}

	VkDeviceSize Allocation::AvailableMemory() const noexcept {
		return availSize;
	}

	const VkDeviceMemory& Allocation::Memory() const noexcept {
		return memory;
	}

	ValidationCode Allocation::Validate() const {

		if (memory == VK_NULL_HANDLE) {
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
		if (availSize < allocation_size) {
			// not enough space in this allocation object
			return false;
		}

		const size_t numFreeSuballocs = availSuballocations.size();

		// use lower_bound to find location of avail suballocation

		size_t avail_idx;
		for (auto iter = availSuballocations.cbegin(); iter != availSuballocations.cend(); ++iter) {
			if ((*iter)->size < allocation_size) {
				avail_idx = iter - availSuballocations.cbegin();
				break;
			}
		}

		for (size_t idx = avail_idx; idx < numFreeSuballocs; ++idx) {
			VkDeviceSize offset = 0;
			const auto iter = availSuballocations[idx];
			// Check allocation for validity
			bool allocation_valid = VerifySuballocation(buffer_image_granularity, allocation_size, allocation_alignment, allocation_type, iter, &offset);
			if (allocation_valid) {
				dest_request->freeSuballocation = iter;
				dest_request->offset = offset;
				return true;
			}
		}

		return false;
	}

	bool Allocation::VerifySuballocation(const VkDeviceSize & buffer_image_granularity, const VkDeviceSize & allocation_size, const VkDeviceSize & allocation_alignment, SuballocationType allocation_type, const suballocationList::const_iterator & dest_suballocation_location, VkDeviceSize * dest_offset) const {
		assert(allocation_size > 0);
		assert(allocation_type != SuballocationType::Free);
		assert(dest_suballocation_location != Suballocations.cend());
		assert(dest_offset != nullptr);

		const Suballocation& suballoc = *dest_suballocation_location;
		assert(suballoc.type != SuballocationType::Free);

		if (suballoc.size < allocation_size) {
			return false;
		}

		*dest_offset = suballoc.offset;

		// Apply alignment
		*dest_offset = AlignUp<VkDeviceSize>(*dest_offset, allocation_alignment);

		// check previous suballocations for conflicts with buffer-image granularity, change alignment as needed
		if (buffer_image_granularity > 1) {
			bool conflict_found = false;
			// iterate backwards, since we're checking previous suballoations for alignment conflicts
			auto prev_suballoc_iter = dest_suballocation_location;
			while (prev_suballoc_iter != Suballocations.cbegin()) {
				--prev_suballoc_iter;
				const Suballocation& prev_suballoc = *prev_suballoc_iter;
				bool on_same_page = CheckBlocksOnSamePage(prev_suballoc.offset, prev_suballoc.size, *dest_offset, buffer_image_granularity);
				if (on_same_page) {
					conflict_found = CheckBufferImageGranularityConflict(prev_suballoc.type, allocation_type);
					if (conflict_found) {
						break;
					}
				}
				else {
					break;
				}
			}
			if (conflict_found) {
				// align up by a page size to get off the current page and remove the conflict.
				*dest_offset = AlignUp<VkDeviceSize>(*dest_offset, buffer_image_granularity);
			}
		}

		// calculate padding at beginning from offset
		const VkDeviceSize padding_begin = *dest_offset - suballoc.offset;

		// calculate required padding at end, assuming current suballoc isn't at end of memory object
		auto next_iter = dest_suballocation_location;
		++next_iter;
		const VkDeviceSize padding_end = (next_iter != Suballocations.cend()) ? DEBUG_PADDING : 0;

		// Can't allocate if padding at begin and end is greater than requested size.
		if (padding_begin + padding_end > allocation_size) {
			return false;
		}

		// We checked previous allocations for conflicts: now, we'll check next suballocations
		if(buffer_image_granularity > 1) {
			auto next_iter = dest_suballocation_location;
			++next_iter;
			while (next_iter != Suballocations.cend()) {
				const auto& next_suballoc = *next_iter;
				bool on_same_page = CheckBlocksOnSamePage(*dest_offset, allocation_size, next_suballoc.offset, buffer_image_granularity);
				if (on_same_page) {
					if (CheckBufferImageGranularityConflict(allocation_type, next_suballoc.type)) {
						return false;
					}
				}
				else {
					break;
				}
				++next_iter;
			}
		}

		return true;
	}

	bool Allocation::Empty() const {
		return Suballocations.empty();
	}

	void Allocation::Allocate(const SuballocationRequest & request, const SuballocationType & allocation_type, const VkDeviceSize & allocation_size) {
		assert(request.freeSuballocation != Suballocations.cend());
		Suballocation& suballoc = *request.freeSuballocation;
		assert(suballoc.type == SuballocationType::Free); 

		const VkDeviceSize padding_begin = request.offset - suballoc.offset;
		const VkDeviceSize padding_end = suballoc.size - padding_begin - allocation_size;

		removeFreeSuballocation(request.freeSuballocation);
		--freeCount;
		availSize -= allocation_size;
		suballoc.offset = request.offset;
		suballoc.size = allocation_size;
		suballoc.type = allocation_type;

		// if there's any remaining memory after this allocation, register it

		if (padding_end) {
			Suballocation padding_suballoc{ request.offset + allocation_size, padding_end, SuballocationType::Free };
			auto next_iter = request.freeSuballocation;
			++next_iter;
			const auto insert_iter = Suballocations.insert(next_iter, padding_suballoc);
			// insert_iter returns iterator giving location of inserted item
			insertFreeSuballocation(insert_iter);
			++freeCount;
			// TODO: Verify that we should be doing this
			availSize += padding_end;
		}

		// if there's any remaining memory before the allocation, register it.
		if (padding_begin) {
			Suballocation padding_suballoc{ request.offset - padding_begin, padding_begin, SuballocationType::Free };
			auto next_iter = request.freeSuballocation;
			++next_iter;
			const auto insert_iter = Suballocations.insert(next_iter, padding_suballoc);
			insertFreeSuballocation(insert_iter);
			++freeCount;
			availSize += padding_begin;
		}

	}

	void Allocation::Free(const VkMappedMemoryRange * memory_to_free) {
		// Choose search direction based based on size of object to free
		const bool forward_direction = (memory_to_free->offset) < (Size / 2);
		if (forward_direction) {
			for (auto iter = Suballocations.begin(); iter != Suballocations.end(); ++iter) {
				auto& suballoc = *iter;
				if (suballoc.offset == memory_to_free->offset) {
					freeSuballocation(iter);
					return;
				}
			}
		}
		else {
			auto iter = Suballocations.end();
			--iter;
			for (; iter != Suballocations.begin(); --iter) {
				auto& suballoc = *iter;
				if (suballoc.offset == memory_to_free->offset) {
					freeSuballocation(iter);
					return;
				}
			}
		}
	}

	VkDeviceSize Allocation::LargestAvailRegion() const noexcept {
		return (*availSuballocations.front()).size;
	}

	suballocation_iterator_t Allocation::begin() {
		return Suballocations.begin();
	}

	suballocation_iterator_t Allocation::end() {
		return Suballocations.end();
	}

	const_suballocation_iterator_t Allocation::begin() const {
		return Suballocations.begin();
	}

	const_suballocation_iterator_t Allocation::end() const {
		return Suballocations.end();
	}

	const_suballocation_iterator_t Allocation::cbegin() const {
		return Suballocations.cbegin();
	}

	const_suballocation_iterator_t Allocation::cend() const {
		return Suballocations.cend();
	}

	avail_suballocation_iterator_t Allocation::avail_begin() {
		return availSuballocations.begin();
	}

	avail_suballocation_iterator_t Allocation::avail_end() {
		return availSuballocations.end();
	}

	const_avail_suballocation_iterator_t Allocation::avail_begin() const {
		return availSuballocations.begin();
	}

	const_avail_suballocation_iterator_t Allocation::avail_end() const {
		return availSuballocations.end();;
	}

	const_avail_suballocation_iterator_t Allocation::avail_cbegin() const {
		return availSuballocations.cbegin();
	}

	const_avail_suballocation_iterator_t Allocation::avail_cend() const {
		return availSuballocations.cend();;
	}

	void Allocation::mergeFreeWithNext(const suballocationList::iterator & item_to_merge) {
		auto next_iter = item_to_merge;
		++next_iter;
		assert(next_iter != Suballocations.cend());
		// add item to merge's size to the size of the object after it
		next_iter->size += item_to_merge->size;
		--freeCount;
		Suballocations.erase(item_to_merge);
	}

	void Allocation::freeSuballocation(const suballocationList::iterator & item_to_free) {
		Suballocation& suballoc = *item_to_free;
		suballoc.type = SuballocationType::Free;

		++freeCount;
		availSize += suballoc.size;

		bool merge_next = false, merge_prev = false;

		auto next_iter = item_to_free;
		++next_iter;
		if ((next_iter != Suballocations.cend()) && (next_iter->type == SuballocationType::Free)) {
			merge_next = true;
		}

		auto prev_iter = item_to_free;
		
		if (prev_iter != Suballocations.cbegin()) {
			--prev_iter;
			if (prev_iter->type == SuballocationType::Free) {
				merge_prev = true;
			}
		}

		if (merge_next) {
			removeFreeSuballocation(next_iter);
			mergeFreeWithNext(item_to_free);
		}

		if (merge_prev) {
			removeFreeSuballocation(prev_iter);
			mergeFreeWithNext(prev_iter);
			insertFreeSuballocation(prev_iter);
		}
		else {
			insertFreeSuballocation(item_to_free);
		}
	}

	void Allocation::insertFreeSuballocation(const suballocationList::iterator & item_to_insert) {
		if (item_to_insert->size >= MinSuballocationSizeToRegister) {
			if (availSuballocations.empty()) {
				availSuballocations.push_back(item_to_insert);
			}
			else {
				// find correct position ot insert "item_to_insert" and do so.
				auto insert_iter = std::lower_bound(availSuballocations.begin(), availSuballocations.end(), item_to_insert, suballocIterCompare());
				availSuballocations.insert(insert_iter, item_to_insert);
			}
		}

	}

	void Allocation::removeFreeSuballocation(const suballocationList::iterator & item_to_remove) {
		if (item_to_remove->size >= MinSuballocationSizeToRegister) {
			auto remove_iter = std::remove(availSuballocations.begin(), availSuballocations.end(), item_to_remove);
			availSuballocations.erase(remove_iter, availSuballocations.end());
		}

	}

	AllocationCollection::AllocationCollection(Allocator * _allocator) : allocator(_allocator) {}

	AllocationCollection::~AllocationCollection() {
		for (size_t i = 0; i < allocations.size(); ++i) {
			allocations[i]->Destroy(allocator);
			delete allocations[i];
		}
	}

	Allocation * AllocationCollection::operator[](const size_t & idx) {
		return allocations[idx];
	}

	const Allocation * AllocationCollection::operator[](const size_t & idx) const {
		return allocations[idx];
	}

	bool AllocationCollection::Empty() const {
		return allocations.empty();
	}

	size_t AllocationCollection::Free(const VkMappedMemoryRange * memory_to_free) {
		bool forward_direction = memory_to_free->size >= availSize / 2;
		size_t allocation_index = 0;
		if (forward_direction) {
			for (auto iter = allocations.begin(); iter != allocations.end(); ++iter) {
				if ((*iter)->Memory() == memory_to_free->memory) {
					(*iter)->Free(memory_to_free);
					availSize -= memory_to_free->size;
					return allocation_index;
				}
				++allocation_index;
			}
		}
		else {
			allocation_index = allocations.size() - 1;
			for (auto iter = allocations.rbegin(); iter != allocations.rend(); ++iter) {
				if ((*iter)->Memory() == memory_to_free->memory) {
					(*iter)->Free(memory_to_free);
					availSize -= memory_to_free->size;
					return allocation_index;
				}
				--allocation_index;
			}
		}

		return std::numeric_limits<size_t>::max();
	}

	void AllocationCollection::SortAllocations() {
		// sorts collection so that allocation with most free space is first.
		std::sort(allocations.begin(), allocations.end());
		// update total avail size
		availSize = 0;
		for (auto iter = allocations.begin(); iter != allocations.end(); ++iter) {
			availSize += (*iter)->AvailableMemory();
		}
	}

	Allocator::Allocator(const Device * parent_dvc) : parent(parent_dvc) {
		deviceProperties = parent->GetPhysicalDevice().Properties;
		deviceMemoryProperties = parent->GetPhysicalDevice().MemoryProperties;

		// initialize base pools, one per memory type.
		for (size_t i = 0; i < GetMemoryTypeCount(); ++i) {
			allocations[i] = new AllocationCollection(this);
		}
	}

	Allocator::~Allocator() {

		/*
			Delete images and buffers.
		*/

		for (auto iter = imageToMemoryMap.begin(); iter != imageToMemoryMap.end(); ++iter) {
			vkDestroyImage(parent->vkHandle(), iter->first, nullptr);
		}

		for (auto iter = bufferToMemoryMap.begin(); iter != bufferToMemoryMap.end(); ++iter) {
			vkDestroyBuffer(parent->vkHandle(), iter->first, nullptr);
		}

		/*
			Delete collections: destructors of these should take care of the rest.
		*/

		for (size_t i = 0; i < GetMemoryTypeCount(); ++i) {
			delete allocations[i];
		}
	}

	VkDeviceSize Allocator::GetPreferredBlockSize(const uint32_t& memory_type_idx) const noexcept {
		VkDeviceSize heapSize = deviceMemoryProperties.memoryHeaps[deviceMemoryProperties.memoryTypes[memory_type_idx].heapIndex].size;
		return (heapSize <= SmallHeapMaxSize) ? preferredSmallHeapBlockSize : preferredLargeHeapBlockSize;
	}

	VkDeviceSize Allocator::GetBufferImageGranularity() const noexcept {
		return deviceProperties.limits.bufferImageGranularity;
	}

	uint32_t Allocator::GetMemoryHeapCount() const noexcept {
		return deviceMemoryProperties.memoryHeapCount;
	}

	uint32_t Allocator::GetMemoryTypeCount() const noexcept {
		return deviceMemoryProperties.memoryTypeCount;
	}

	const VkDevice & Allocator::DeviceHandle() const noexcept {
		return parent->vkHandle();
	}

	VkResult Allocator::AllocateMemory(const VkMemoryRequirements& memory_reqs, const AllocationRequirements& alloc_details, const SuballocationType& suballoc_type, VkMappedMemoryRange* dest_memory_range, uint32_t* dest_memory_type_idx) {
		
		// find memory type (i.e idx) required for this allocation
		uint32_t memory_type_idx = findMemoryTypeIdx(memory_reqs, alloc_details);
		if (memory_type_idx != std::numeric_limits<uint32_t>::max()) {
			return allocateMemoryType(memory_reqs, alloc_details, memory_type_idx, suballoc_type, dest_memory_range);
		}
	}

	void Allocator::FreeMemory(const VkMappedMemoryRange * memory_to_free) {
		
		Allocation* alloc_to_delete = nullptr;
		bool found = false; // searching for given memory range.
		for (uint32_t type_idx = 0; type_idx = GetMemoryTypeCount(); ++type_idx) {
			auto& allocation_collection = allocations[type_idx];
			const size_t alloc_idx = allocation_collection->Free(memory_to_free);
			if (alloc_idx != std::numeric_limits<size_t>::max()) {
				LOG(INFO) << "Memory freed successfully.";
				found = true;
				auto& alloc = allocation_collection->allocations[alloc_idx];
				// did freeing the given memory made "alloc" empty?
				if (alloc->Empty()) {
					// alloc is now empty: but, we already have an empty allocation instance
					// for this memory type index, so remove it from the vector and specify 
					// that we'll fully delete it.
					if (emptyAllocations[type_idx]) {
						// remove allocation.
						alloc_to_delete = alloc;
						allocation_collection->allocations.erase(allocation_collection->allocations.begin() + alloc_idx);
						break;
					}
					else {
						// alloc is now the empty allocation at that index.
						emptyAllocations[type_idx] = true;
					}
				}
				// re-sort the collection.
				allocation_collection->SortAllocations();
				break;
			}
		}

		if (found) {
			if (alloc_to_delete != nullptr) {
				LOG(INFO) << "Deleted an allocation.";
				// need to cleanup resources first, before deleting the actual object.
				alloc_to_delete->Destroy(this);
				delete alloc_to_delete;
				return;
			}
		}

		// memory_to_free not found, possible a privately/singularly allocated memory object
		if (freePrivateMemory(memory_to_free)) {
			return;
		}

		LOG(ERROR) << "Failed to free memory.";
		return;
	}

	uint32_t Allocator::findMemoryTypeIdx(const VkMemoryRequirements& mem_reqs, const AllocationRequirements & details) const noexcept {
		auto req_flags = details.requiredFlags;
		auto preferred_flags = details.preferredFlags;
		if (req_flags == 0) {
			assert(preferred_flags != VkMemoryPropertyFlagBits(0));
			req_flags = preferred_flags;
		}

		uint32_t min_cost = std::numeric_limits<uint32_t>::max();
		uint32_t result_idx = std::numeric_limits<uint32_t>::max();
		// preferred_flags, if not zero, must be a subset of req_flags
		for (uint32_t type_idx = 0, memory_type_bit = 1; type_idx < GetMemoryTypeCount(); ++type_idx) {
			// memory type of idx is acceptable according to mem_reqs
			if ((memory_type_bit & mem_reqs.memoryTypeBits) != 0) {
				const VkMemoryPropertyFlags& curr_flags = deviceMemoryProperties.memoryTypes[type_idx].propertyFlags;
				// current type contains required flags.
				if ((req_flags & curr_flags) == 0) {
					// calculate the cost of the memory type as the number of bits from preferred_flags
					// not present in current type at type_idx.
					uint32_t cost = countBitsSet(preferred_flags & ~req_flags);
					if (cost < min_cost) {
						result_idx = type_idx;
						// ideal memory type, return it.
						if (cost == 0) {
							return result_idx;
						}
						min_cost = cost;
					}
				}

			}
		}
		// didn't find zero "cost" idx, but return it.
		// this means that any methods that call this particular type finding method should handle the "exception"
		// of invalid indices themselves.
		return result_idx;
	}

	VkResult Allocator::allocateMemoryType(const VkMemoryRequirements & memory_reqs, const AllocationRequirements & alloc_details, const uint32_t & memory_type_idx, const SuballocationType & type, VkMappedMemoryRange * dest_memory_range) {
		*dest_memory_range = VkMappedMemoryRange{ VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, nullptr, VK_NULL_HANDLE, 0, memory_reqs.size };
		
		const VkDeviceSize preferredBlockSize = GetPreferredBlockSize(memory_type_idx);

		const bool private_memory = alloc_details.privateMemory || memory_reqs.size > preferredBlockSize / 2;

		if (private_memory) {
			if (AllocationRequirements::noNewAllocations) {
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			}
			else {
				return allocatePrivateMemory(memory_reqs.size, type, memory_type_idx, dest_memory_range);
			}
		}
		else {
			auto& alloc_collection = allocations[memory_type_idx];

			// first, check existing allocations
			for (auto iter = alloc_collection->allocations.cbegin(); iter != alloc_collection->allocations.cend(); ++iter) {
				SuballocationRequest request;
				const auto& alloc = *iter;
				if (alloc->RequestSuballocation(GetBufferImageGranularity(), memory_reqs.size, memory_reqs.alignment, type, &request)) {
					if (alloc->Empty()) {
						emptyAllocations[memory_type_idx] = false;
					}

					alloc->Allocate(request, type, memory_reqs.size);
					dest_memory_range->memory = alloc->Memory();
					dest_memory_range->offset = request.offset;
					if (VALIDATE_MEMORY) {
						ValidationCode result_code = alloc->Validate();
						if (result_code != ValidationCode::VALIDATION_PASSED) {
							LOG(ERROR) << "Validation of new allocation failed with reason: " << result_code;
						}
					}
					return VK_SUCCESS;
				}
			}

			// search didn't pass: create new allocation.
			if (AllocationRequirements::noNewAllocations) {
				LOG(ERROR) << "All available allocations full or invalid, and requested allocation not allowed to be private!";
				return VK_ERROR_OUT_OF_DEVICE_MEMORY;
			}
			else {
				VkMemoryAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, preferredBlockSize, memory_type_idx };
				VkDeviceMemory new_memory = VK_NULL_HANDLE;
				VkResult result = vkAllocateMemory(parent->vkHandle(), &alloc_info, nullptr, &new_memory);
				assert(result != VK_ERROR_OUT_OF_DEVICE_MEMORY); // make sure we're not over-allocating and using all device memory.
				if (result != VK_SUCCESS) {
					// halve allocation size
					alloc_info.allocationSize /= 2;
					if (alloc_info.allocationSize >= memory_reqs.size) {
						result = vkAllocateMemory(parent->vkHandle(), &alloc_info, nullptr, &new_memory);
						if (result != VK_SUCCESS) {
							alloc_info.allocationSize /= 2;
							if (alloc_info.allocationSize >= memory_reqs.size) {
								result = vkAllocateMemory(parent->vkHandle(), &alloc_info, nullptr, &new_memory);
							}
						}
					}
				}
				// if still not allocated, try allocating private memory (if allowed)
				if (result != VK_SUCCESS && alloc_details.privateMemory) {
					result = allocatePrivateMemory(memory_reqs.size, type, memory_type_idx, dest_memory_range);
					if (result == VK_SUCCESS) {
						LOG(INFO) << "Allocation of memory succeeded";
						return VK_SUCCESS;
					}
					else {
						LOG(WARNING) << "Allocation of memory failed, after multiple attempts.";
						return result;
					}
				}

				Allocation* alloc = new Allocation(this);
				// allocation size is more up-to-date than mem reqs size
				alloc->Init(new_memory, alloc_info.allocationSize);
				alloc_collection->allocations.push_back(alloc);

				SuballocationRequest request{ *alloc->avail_begin(), 0 };
				alloc->Allocate(request, type, memory_reqs.size);
				dest_memory_range->memory = new_memory;
				dest_memory_range->offset = request.offset;
				if (VALIDATE_MEMORY) {
					ValidationCode result_code = alloc->Validate();
					if (result_code != ValidationCode::VALIDATION_PASSED) {
						LOG(ERROR) << "Validation of new allocation failed with reason: " << result_code;
					}
				}

				LOG(INFO) << "Created new allocation object w/ size of " << std::to_string(alloc_info.allocationSize);
				return VK_SUCCESS;
			}
		}

	}

	VkResult Allocator::allocatePrivateMemory(const VkDeviceSize & size, const SuballocationType & type, const uint32_t & memory_type_idx, VkMappedMemoryRange * memory_range) {
		return VK_ERROR_VALIDATION_FAILED_EXT;
	}

	bool Allocator::freePrivateMemory(const VkMappedMemoryRange * memory_to_free) {
		return false;
	}

	VkResult Allocator::AllocateForImage(VkImage & image_handle, const AllocationRequirements & details, const SuballocationType & alloc_type, VkMappedMemoryRange * dest_memory_range, uint32_t * memory_type_idx) {

		// Get memory info.
		VkMemoryRequirements memreqs;
		vkGetImageMemoryRequirements(parent->vkHandle(), image_handle, &memreqs);

		return AllocateMemory(memreqs, details, alloc_type, dest_memory_range, memory_type_idx);
	}

	VkResult Allocator::AllocateForBuffer(VkBuffer & buffer_handle, const AllocationRequirements & details, const SuballocationType & alloc_type, VkMappedMemoryRange * dest_memory_range, uint32_t * memory_type_idx) {
		VkMemoryRequirements memreqs;
		vkGetBufferMemoryRequirements(parent->vkHandle(), buffer_handle, &memreqs);
		return AllocateMemory(memreqs, details, alloc_type, dest_memory_range, memory_type_idx);
	}

	VkResult Allocator::CreateImage(VkImage * image_handle, VkMappedMemoryRange * dest_memory_range, const VkImageCreateInfo * img_create_info, const AllocationRequirements & alloc_reqs) {
		VkMappedMemoryRange mem_range{};

		{
			// create image object first.
			VkResult result = vkCreateImage(parent->vkHandle(), img_create_info, nullptr, image_handle);
			VkAssert(result);
		}

		{
			// allocate memory.
			uint32_t type_idx = 0;
			
			SuballocationType suballoc_type = img_create_info->tiling == VK_IMAGE_TILING_OPTIMAL ? SuballocationType::ImageOptimal : SuballocationType::ImageLinear;
			VkResult result = AllocateForImage(*image_handle, alloc_reqs, suballoc_type, &mem_range, &type_idx);
			VkAssert(result);
		}
		
		{
			// bind memory to image
			if (dest_memory_range != nullptr) {
				// update memory range
				*dest_memory_range = mem_range;
			}
			VkResult result = vkBindImageMemory(parent->vkHandle(), *image_handle, mem_range.memory, mem_range.offset);
			VkAssert(result);

			// add to map
			imageToMemoryMap.insert(std::make_pair(*image_handle, mem_range));
			return VK_SUCCESS;
		}

		// shouldn't reach here: temporary while add in additional paths to above code
		return VK_ERROR_VALIDATION_FAILED_EXT;
	}

	VkResult Allocator::CreateBuffer(VkBuffer * buffer_handle, VkMappedMemoryRange * dest_memory_range, const VkBufferCreateInfo * buffer_create_info, const AllocationRequirements & alloc_reqs) {

		// create buffer object first
		VkResult result = vkCreateBuffer(parent->vkHandle(), buffer_create_info, nullptr, buffer_handle);
		VkAssert(result);

		// allocate memory
		uint32_t type_idx = 0;
		VkMappedMemoryRange mem_range{};
		result = AllocateForBuffer(*buffer_handle, alloc_reqs, SuballocationType::Buffer, &mem_range, &type_idx);
		VkAssert(result);

		// bind memory
		if (dest_memory_range != nullptr) {
			*dest_memory_range = mem_range;
		}
		result = vkBindBufferMemory(parent->vkHandle(), *buffer_handle, mem_range.memory, mem_range.offset);
		VkAssert(result);

		return VK_SUCCESS;
	}

	void Allocator::DestroyImage(const VkImage & image_handle) {
		if (image_handle == VK_NULL_HANDLE) {
			LOG(ERROR) << "Tried to destroy null image object.";
			throw std::runtime_error("Cannot destroy null image objects.");
		}

		VkMappedMemoryRange mem_range{};
		auto search = imageToMemoryMap.find(image_handle);
		if (search == imageToMemoryMap.end()) {
			LOG(WARNING) << "Couldn't find image to delete in allocator's image-memory map.";
			return;
		}

		// get memory range to erase
		mem_range = search->second;

		// remove handle from map.
		imageToMemoryMap.erase(image_handle);

		// delete handle.
		vkDestroyImage(parent->vkHandle(), image_handle, nullptr);

		// Free memory previously tied to handle.
		FreeMemory(&mem_range);
	}

	void Allocator::DestroyBuffer(const VkBuffer & buffer_handle) {
		if (buffer_handle == VK_NULL_HANDLE) {
			LOG(ERROR) << "Tried to destroy null buffer object.";
			throw std::runtime_error("Cannot destroy null buffer objects.");
		}

		VkMappedMemoryRange range_to_free{};
		auto search = bufferToMemoryMap.find(buffer_handle);
		if (search == bufferToMemoryMap.end()) {
			LOG(WARNING) << "Couldn't find buffer/buffer's memory in allocator's buffer-memory map.";
			return;
		}

		range_to_free = search->second;
		bufferToMemoryMap.erase(search);

		vkDestroyBuffer(parent->vkHandle(), buffer_handle, nullptr);
		FreeMemory(&range_to_free);

	}

}