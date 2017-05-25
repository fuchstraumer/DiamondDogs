#ifndef VULPES_UTIL_CIRCULAR_BUFFER_H
#define VULPES_UTIL_CIRCULAR_BUFFER_H

#include <memory>
#include <iterator>
#include <type_traits>

namespace vulpes {

    namespace util {


        template<typename T, size_t capacity, typename allocator = std::allocator<T>> 
        class circular_buffer {

			typedef allocator allocator_type;
            typedef typename allocator::value_type value_type;
            typedef typename allocator::pointer pointer;
            typedef typename allocator::const_pointer const_pointer;
            typedef typename allocator::reference reference;
            typedef typename allocator::const_reference const_reference;
            typedef typename allocator::size_type size_type;
            typedef typename allocator::difference_type difference_type;
			typedef circular_buffer self_type;

            circular_buffer(const circular_buffer&) = delete;
            circular_buffer& operator=(const circular_buffer&) = delete;

            circular_buffer() = delete;

			template<typename T, typename elem_type = typename T::value_type>
			class circular_buffer_iter {

				typedef T buff_type;
				typedef elem_type value_type;
				typedef T::reference reference;
				typedef T::const_reference const_reference;
				typedef T::pointer pointer;
				typedef T::const_pointer const_pointer;
				typedef size_t size_type;
				typedef ptrdiff_t difference_type;

			public:

				circular_buffer_iter(buff_type *_buffer, size_t _start_position) : buffer(_buffer), start_position(_start_position) {}

				circular_buffer_iter(const circular_buffer_iter& other) : buffer(other.buffer), start_position(other.start_position), 

				value_type& operator*() {
					return *buffer[position];
				}

				value_type* operator->() {
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
				buff_type* buffer;
				size_t start_position;
				size_t position;
			};

			typedef typename circular_buffer_iter<self_type, value_type> iterator;
			typedef typename const circular_buffer_iter<const self_type, const value_type> const_iterator;

        public:

            explicit circular_buffer(const size_type& capacity) : data(allocator.allocate(capacity)), num_elements(0), head_idx(0), tail_idx(0) {}
            
            ~circular_buffer() {
                allocator.deallocate(capacity);
            }

            void push_back(T item) {
                if(num_elements == 0){
                    data[head_idx] = std::move(item);
                    tail_idx = head_idx;
                    ++num_elements;
                }
                else if (num_elements != capacity) {
                    tail_increment();
                    data[tail_idx] = std::move(item);
                }
                else {
                    // element at back lost, replaced with "item"
                    head_increment();
                    tail_increment();
                    data[tail_idx] = std::move(item);
                }
            }

            size_type size() const noexcept {
                return num_elements;
            }

            size_type capacity() const noexcept {
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
                if(idx < capacity || idx > capacity) {
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

        private:

            iterator begin() {
                return iterator(this, 0);
            }

            iterator end() {
                return iterator(this, size());
            }

            reference operator[](const size_type& idx) { 
                return data[idx];
            }

            reference at(const size_t& idx) noexcept {
                if(idx < capacity || idx > capacity) {
                    return data[idx];
                }
                return data[head_idx];
            }

            // update tail index so we can pop/push at front
            void tail_increment() {
                ++tail_idx;
                ++num_elements;
                if(tail_idx == capacity) {
                    tail_idx = 0;
                }
            }

            void head_increment() {
                if(empty()){ return; }
                ++head_idx;
                --num_elements;
                if(head_idx == capacity) {
                    head_idx = 0;
                }
            }

			/*
				Data members
			*/

			pointer data;
			size_type memory_size;
			size_type head_idx;
			size_type tail_idx;
			size_type num_elements;

        };


    }
}

#endif //!VULPES_UTIL_CIRCULAR_BUFFER_H