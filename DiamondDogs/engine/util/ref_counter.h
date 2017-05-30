#pragma once
#ifndef VULPES_VK_UTIL_REF_COUNTER_H
#include "stdafx.h"

namespace vulpes {

	/*
		
		Class - reference counted objects inherit from this. forms simple base ref_counter, however. 

	*/
	namespace detail {

		class ref_counter {
		public:
			
			ref_counter() = default;
			ref_counter(const ref_counter& other) noexcept : ref_count(other.ref_count) { ++(*ref_count); }
			ref_counter(ref_counter&& other) noexcept : ref_count(std::move(other.ref_count)) {}

			ref_counter& operator=(const ref_counter& other) {
				ref_count = other.ref_count;
				++(*ref_count);
			}

			ref_counter& operator=(ref_counter&& other) {
				ref_count = std::move(other.ref_count);
			}

			virtual ~ref_counter() {}

		protected:
			uint32_t get_ref_count() const noexcept { return *ref_count; }
			std::shared_ptr<uint32_t> ref_count = std::make_shared<uint32_t>(1);
		};



	}

}

#endif // !VULPES_VK_UTIL_REF_COUNTER_H
