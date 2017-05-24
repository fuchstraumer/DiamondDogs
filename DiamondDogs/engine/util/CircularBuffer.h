#ifndef VULPES_UTIL_CIRCULAR_BUFFER_H
#define VULPES_UTIL_CIRCULAR_BUFFER_H

#include "stdafx.h"

namespace vulpes {

    namespace util {
        
        namespace detail {
            template<typename T>
            class circular_buffer_iter {

                typedef T buff_type;
                typedef T::value_type value_type;
                typedef T::reference reference;
                typedef T::const_reference const_reference;
                typedef T::pointer pointer;
                typedef T::const_pointer const_pointer;
                typedef size_t size_type;
                typedef ptrdiff_t difference_type;

            public:

                circular_buffer_iter(buff_type *_buffer, size_t _start_position) : buffer(_buffer), start_position(_start_position) {}
                
                T& operator*() {
                    return *buffer[position];
                }

                T* operator->() {
                    return &(operator*());
                }

            private:
                buff_type* buffer;
                size_t start_position;
                size_t position;
            };
        }

        template<typename T, size_t num, typename allocator = std::allocator<T>> 
        class CircularBuffer {

            typedef T value_type;
            typedef T* pointer;
            typedef const T* const_pointer;
            typedef T& reference;
            typedef const T& const_reference;
            typedef size_t size_type;
            typedef ptrdiff_t difference_type;
            typedef CircularBuffer self_type;
            typedef circular_buffer_iter<self_type> iterator;
            typedef const circular_buffer_iter<self_type> const_iterator;

            pointer data;
            size_type memory_size;
            size_type head_idx;
            size_type tail_idx;
            size_type num_elements;

            CircularBuffer(const CircularBuffer&) = delete;
            CircularBuffer& operator=(const CircularBuffer&) = delete;

            CircularBuffer() = delete;

        public:

            explicit CircularBuffer(const size_type& capacity) : data(allocator.allocate(num)), num_elements(0), head_idx(0), tail_idx(0) {}
            
            ~CircularBuffer() {
                allocator.deallocate(num);
            }

            void push_back(T item) {
                if(num_elements == 0){
                    data[head_idx] = std::move(item);
                    tail_idx = head_idx;
                    ++num_elements;
                }
                else if (num_elements != num) {
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
                return num;
            }

            size_type memory_footprint() const noexcept {
                return sizeof(T) * num;
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
                if(idx < num || idx > num) {
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
                if(idx < num || idx > num) {
                    return data[idx];
                }
                return data[head_idx];
            }

            // update tail index so we can pop/push at front
            void tail_increment() {
                ++tail_idx;
                ++num_elements;
                if(tail_idx == num) {
                    tail_idx = 0;
                }
            }

            void head_increment() {
                if(empty()){ return; }
                ++head_idx;
                --num_elements;
                if(head_idx == num) {
                    head_idx = 0;
                }
            }

        };


    }
}

#endif //!VULPES_UTIL_CIRCULAR_BUFFER_H