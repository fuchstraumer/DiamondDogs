#pragma once
#ifndef VULPES_VK_ALLOCATOR_H
#define VULPES_VK_ALLOCATOR_H

#include "stdafx.h"
#include "../ForwardDecl.h"
#include "../NonCopyable.h"

namespace vulpes {

	constexpr static size_t vkMaxMemoryTypes = 32;
	// mininum size of suballoction objects to bother registering in our allocation's list's
	constexpr static VkDeviceSize MinSuballocationSizeToRegister = 16;

	enum class SuballocationType : uint8_t {
		Free = 0, // unused entry
		Unknown, // invalid - how did this get set?
		Buffer,
		ImageUnknown, // image memory without defined tiling (??)
		ImageLinear,
		ImageOptimal,
	};

	enum class MemoryUsage : uint8_t {
		GPU_ONLY,
		CPU_ONLY, // staging buffers, usually
		CPU_TO_GPU, // dynamic objects
		GPU_TO_CPU, // query/compute return data
	};

	enum class ValidationCode : uint8_t {
		VALIDATION_PASSED = 0,
		NULL_MEMORY_HANDLE, // allocation's memory handle is invalid
		ZERO_MEMORY_SIZE, // allocation's memory size is zero
		INCORRECT_SUBALLOC_OFFSET, // Offset of suballocation is incorrect: it may overlap with another, or it may 
		NEED_MERGE_SUBALLOCS, // two adjacent free suballoctions: merge them into one bigger suballocation
		FREE_SUBALLOC_COUNT_MISMATCH, // we found more free suballocations while validating than there are in the free suballoc list
		USED_SUBALLOC_IN_FREE_LIST, // non-free suballocation in free suballoc list
		FREE_SUBALLOC_SORT_INCORRECT, // free suballocation list is sorted by size ascending: sorting is incorrect, re-sort
		FINAL_SIZE_MISMATCH, // calculated offset as sum of all suballoc sizes is not equal to allocations total size
		FINAL_FREE_SIZE_MISMATCH, // calculated total available size doesn't match stored available size
	};

	struct AllocationDetails {
		// true if whatever allocation this belongs to should be in its own device memory object
		VkBool32 privateMemory;
		// when using this, don't set flags. when using flags, don't use this.
		MemoryUsage usage;
		// leave to 0 if using "usage" instead
		VkMemoryPropertyFlags requiredFlags;
		// acts as additional flags over above.
		VkMemoryPropertyFlags preferredFlags;
	};

	struct Suballocation {
		VkDeviceSize offset, size;
		SuballocationType type;
	};

	struct suballocOffsetCompare {
		bool operator()(const Suballocation& s0, const Suballocation& s1) {
			return s0.offset < s1.offset; // true when s0 is before s1
		}
	};

	struct privateSuballocation {
		VkDeviceMemory memory;
		VkDeviceSize size;
		SuballocationType type;
	};

	using suballocationList = std::list<Suballocation>;

	struct SuballocationRequest {
		suballocationList::iterator freeSuballocation; // location of suballoc this request can use.
		VkDeviceSize offset;
	};

	/*
		Allocation class contains singular VkDeviceMemory object.
		Should only have a handful of these at any one time.
	*/

	class Allocation {
	public:

		Allocation(Allocator* alloc);
		~Allocation(); // just assert that memory has been free'd

		// new_memory is a fresh device memory object, new_size is size of this device memory object.
		void Init(VkDeviceMemory& new_memory, const VkDeviceSize& new_size);

		// cleans up resources and prepares object to be safely destroyed.
		void Destroy(Allocator* alloc);

		VkDeviceMemory Memory;
		VkDeviceSize Size;
		uint32_t freeCount;
		VkDeviceSize availSize; // total available size among all sub-allocations.
		suballocationList Suballocations;
		Allocator* allocator;

		// Verifies integrity of memory by checking all contained structs/objects.
		ValidationCode Validate() const;

		bool RequestSuballocation(const VkDeviceSize& buffer_image_granularity, const VkDeviceSize& allocation_size, const VkDeviceSize& allocation_alignment, SuballocationType allocation_type, SuballocationRequest* dest_request);

		// Verifies that requested suballocation can be added to this object, and sets dest_offset to reflect offset of this now-inserted suballocation.
		bool VerifySuballocation(const VkDeviceSize& buffer_image_granularity, const VkDeviceSize& allocation_size, const VkDeviceSize& allocation_alignment, SuballocationType allocation_type,
			suballocationList::const_iterator& dest_suballocation_location, VkDeviceSize* dest_offset);

		bool Empty() const;

		// performs the actual allocation, once "request" has been checked and made valid.
		void Allocate(const SuballocationRequest& request, const SuballocationType& allocation_type, const VkDeviceSize& allocation_size);

		// Frees memory in region specified (i.e frees/destroys a suballocation)
		void Free(const VkMappedMemoryRange* memory_to_free);

	protected:

		// given a free suballocation, this method merges it with the one immediately after it in the list
		// the second item must also be free: this is a method used to collect disparate regions together.
		void mergeFreeWithNext(const suballocationList::iterator& item_to_merge);

		// releases given suballocation, and then merges it with any adjacent blocks if possible.
		void freeSuballocation(const suballocationList::iterator& item_to_free);

		// given a free suballocation, place it in the correct location of our suballocation list.
		void insertFreeSuballocation(const suballocationList::iterator& item_to_insert);

		// given a free suballocation, remove it from the list (if possible)
		void removeFreeSuballocation(const suballocationList::iterator& item_to_remove);


		// Suballocations sorted by available size, only in this vector
		// if available size is greater than a threshold we set shortly.
		std::vector<suballocationList::iterator> availSuballocations;
	};

	struct AllocationCollection {
		std::vector<Allocation*> allocations;

		AllocationCollection(Allocator* allocator);

		bool Empty() const;

		// attempts to free memory: returns index of free'd allocation or -1 if not 
		// able to free or not able to find desired memory
		size_t Free(const VkMappedMemoryRange* memory_to_free);

		// performs single sort step, to order "allocations" so that it is sorted
		// by total available free memory.
		void AllocationSortStep();

		// adds statistics about "allocations" to static stats obj
		void AddStats(); // TODO: Implement this in a cleaner fashion.

	private:
		Allocator* allocator;
	};

	class Allocator : public NonMovable {
	public:

		Allocator();
		~Allocator();

		VkDeviceSize GetPreferredBlockSize() const noexcept;
		VkDeviceSize GetBufferImageGranularity() const noexcept;

		uint32_t GetMemoryHeapCount() const noexcept;
		uint32_t GetMemoryTypeCount() const noexcept;

		void AllocateMemory(const VkMemoryRequirements& memory_reqs, const AllocationDetails& alloc_details, const SuballocationType& suballoc_type, VkMappedMemoryRange* dest_memory_range, uint32_t* dest_memory_type_idx);

		void FreeMemory(VkMappedMemoryRange* memory_to_free);

	private:

		void allocateMemoryType(const VkMemoryRequirements& memory_reqs, const AllocationDetails& alloc_details, const uint32_t& memory_type_idx, const SuballocationType& type, VkMappedMemoryRange* dest_memory_range);

		void allocatePrivateMemory(const VkDeviceSize& size, const SuballocationType& type, const uint32_t& memory_type_idx, VkMappedMemoryRange* memory_range);

		std::array<AllocationCollection, vkMaxMemoryTypes> allocationCollections;
		std::array<std::mutex, vkMaxMemoryTypes> allocationMutexes;

		std::array<privateSuballocation, vkMaxMemoryTypes> privateAllocations;
		std::array<std::mutex, vkMaxMemoryTypes> privateAllocationMutexes;

		/*
		These maps tie an objects handle to its mapped memory range, so we
		can use the handle (even from other objects) to query info about
		the objects memory.
		*/
		std::unordered_map<VkBuffer, VkMappedMemoryRange> bufferToMemoryMap;
		std::array<std::mutex, vkMaxMemoryTypes> buffToMemMapMutexes;
		std::unordered_map<VkImage, VkMappedMemoryRange> imageToMemoryMap;
		std::array<std::mutex, vkMaxMemoryTypes> imToMemMapMutexes;

		const Device* parent;

		VkPhysicalDeviceProperties deviceProperties;
		VkPhysicalDeviceMemoryProperties deviceMemoryProperties;

		VkDeviceSize preferredLargeHeapBlockSize;
		VkDeviceSize preferredSmallHeapBlockSize;
		const VkAllocationCallbacks* pAllocationCallbacks = nullptr;
		
	};

	

}

#endif // !VULPES_VK_ALLOCATOR_H
