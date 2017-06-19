#ifndef VULPES_UTIL_CIRCULAR_BUFFER_H
#define VULPES_UTIL_CIRCULAR_BUFFER_H

#include <memory>
#include <iterator>
#include <type_traits>

namespace vulpes {

    namespace util {


		template<typename T, class Alloc = std::allocator<T>>
		class circular_buffer {

			typedef Alloc allocator_type;
			typedef typename Alloc::value_type value_type;
			typedef typename Alloc::reference reference;
			typedef typename Alloc::pointer pointer;
			typedef typename Alloc::const_pointer const_pointer;
			typedef typename Alloc::const_reference const_reference;
			typedef typename Alloc::size_type size_type;
			typedef circular_buffer<T, Alloc> self_type;
			typedef circular_buffer<T, Alloc>* self_pointer;

			circular_buffer(const circular_buffer&) = delete;
			circular_buffer& operator=(const circular_buffer&) = delete;

			circular_buffer() = delete;

			/*
			circular_buffer_iter begin
			*/
			class circular_buffer_iter {

				typedef typename Alloc::value_type value_type;
				typedef typename Alloc::reference reference;
				typedef typename Alloc::pointer pointer;

				typedef typename Alloc::difference_type difference_type;
				typedef std::random_access_iterator_tag iterator_category;
				typedef typename Alloc::const_pointer const_pointer;
				typedef typename Alloc::const_reference const_reference;

				typedef std::random_access_iterator_tag iterator_category;
			public:

				circular_buffer_iter() = default;

				circular_buffer_iter(self_pointer buffer, size_t _start_position) : buffer(buffer), start_position(_start_position) {}

				circular_buffer_iter(const circular_buffer_iter& other) : buffer(other.buffer), start_position(other.start_position) {}

				circular_buffer_iter(circular_buffer_iter&& other) : buffer(std::move(other.buffer)), start_position(std::move(other.start_position)) {}

				reference operator*() const {
					return (*buffer)[position];
				}

				pointer operator->() const {
					return &(operator*());
				}

				circular_buffer_iter& operator++() {
					++position;
					return *this;
				}

				circular_buffer_iter& operator++(int) {
					circular_buffer_iter tmp(*this);
					++(*this);
					return tmp;
				}

				circular_buffer_iter& operator--() {
					--position;
					return *this;
				}

				circular_buffer_iter& operator--(int) {
					circular_buffer_iter tmp(*this);
					--(*this);
					return *this;
				}

				circular_buffer_iter operator+(difference_type n) {
					circular_buffer_iter result(*this);
					result.position += n;
					return result;
				}

				circular_buffer_iter& operator+=(difference_type n) {
					position += n;
					return *this;
				}

				circular_buffer_iter operator-(difference_type n) {
					circular_buffer_iter result(*this);
					result.position -= n;
					return result;
				}

				circular_buffer_iter& operator-=(difference_type n) {
					position -= n;
					return *this;
				}

				bool operator==(const circular_buffer_iter& other) {
					return buffer == other.buffer;
				}

				bool operator!=(const circular_buffer_iter& other) {
					return buffer != other.buffer;
				}

			private:
				self_pointer buffer;
				size_t start_position;
				size_t position;
			};
			/*
			circular_buffer_iter end
			*/

			/*
			const_circular_buffer_iter begin
			*/
			class const_circular_buffer_iter {

				typedef typename Alloc::difference_type difference_type;
				// we can go access randomly in the circular buffer 
				typedef const std::random_access_iterator_tag iterator_category;

			public:

				const_circular_buffer_iter() = default;

				const_circular_buffer_iter(const self_pointer _buffer, size_t _start_position) : buffer(_buffer), start_position(_start_position) {}

				const_circular_buffer_iter(const circular_buffer_iter& iter) : buffer(iter.buffer), start_position(iter.start_position) {}

				const_circular_buffer_iter(const const_circular_buffer_iter& other) : buffer(other.buffer), start_position(other.start_position) {}

				const_circular_buffer_iter(const_circular_buffer_iter&& other) : buffer(std::move(other.buffer)), start_position(std::move(other.start_position)) {}

				const_reference operator*() {
					return *buffer[position];
				}

				const_pointer operator->() {
					return &(operator*());
				}

				const_circular_buffer_iter& operator++() {
					++position;
					return *this;
				}

				const_circular_buffer_iter& operator++(int) {
					circular_buffer_iter tmp(*this);
					++(*this);
					return tmp;
				}

				const_circular_buffer_iter& operator--() {
					--position;
					return *this;
				}

				const_circular_buffer_iter& operator--(int) {
					circular_buffer_iter tmp(*this);
					--(*this);
					return *this;
				}

				const_circular_buffer_iter operator+(difference_type n) {
					circular_buffer_iter result(*this);
					result.position += n;
					return result;
				}

				const_circular_buffer_iter& operator+=(difference_type n) {
					position += n;
					return *this;
				}

				const_circular_buffer_iter operator-(difference_type n) {
					circular_buffer_iter result(*this);
					result.position -= n;
					return result;
				}

				const_circular_buffer_iter& operator-=(difference_type n) {
					position -= n;
					return *this;
				}

				bool operator==(const const_circular_buffer_iter& other) {
					return buffer == other.buffer;
				}

				bool operator!=(const const_circular_buffer_iter& other) {
					return buffer != other.buffer;
				}

			private:
				const self_pointer buffer;
				size_t start_position;
				size_t position;
			};
			/*
			const_circular_buffer_iter end
			*/

			typedef circular_buffer_iter iterator;
			typedef const_circular_buffer_iter const_iterator;

		public:

			explicit circular_buffer(const size_type& _capacity) : capacity(_capacity), num_elements(0), head_idx(1), tail_idx(0) {
				data = alloc.allocate(capacity);
			}

			~circular_buffer() {
				alloc.deallocate(data, capacity);
			}

			void push_back(T item) {
				if (num_elements == capacity) {
					head_increment();
				}
				data[tail_idx] = std::move(item);
				tail_increment();
			}

			size_type size() const noexcept {
				return num_elements;
			}

			size_type element_capacity() const noexcept {
				return capacity;
			}

			size_type memory_footprint() const noexcept {
				return sizeof(T) * capacity;
			}

			bool empty() const noexcept {
				return num_elements == 0;
			}

			// only const_ref dta access allowed to avoid accidentally writing/modifying contents
			// w/o using push_back(). maintains circular buffer conformance (?)

			const_reference operator[](const size_type& idx) const {
				return data[idx];
			}

			// returns front element if at() is out of range
			const_reference at(const size_t& idx) const noexcept {
				if (idx < capacity || idx > capacity) {
					return data[idx];
				}
				return data[head_idx];
			}

			reference operator[](const size_type& idx) {
				return data[idx];
			}

			reference at(const size_t& idx) noexcept {
				if (idx < capacity || idx > capacity) {
					return data[idx];
				}
				return data[head_idx];
			}


			reference front() {
				return data[head_idx];
			}

			reference back() {
				return data[tail_idx];
			}

			const_reference front() const {
				return data[head_idx];
			}

			const_reference back() const {
				return data[tail_idx];
			}

			void clear() {
				head_idx = tail_idx = num_elements = 0;
			}

			iterator begin() {
				return iterator(this, 0);
			}

			iterator end() {
				return iterator(this, size());
			}

		private:



			// update tail index so we can pop/push at front
			void tail_increment() {
				++tail_idx;
				++num_elements;
				if (tail_idx == capacity) {
					tail_idx = 0;
				}
			}

			void head_increment() {
				++head_idx;
				--num_elements;
				if (head_idx == capacity) {
					head_idx = 0;
				}
			}

			/*
			Data members
			*/

			pointer data;
			size_type head_idx;
			size_type tail_idx;
			size_type capacity;
			size_type num_elements;
			allocator_type alloc;
		};


    }
}

#endif //!VULPES_UTIL_CIRCULAR_BUFFER_H