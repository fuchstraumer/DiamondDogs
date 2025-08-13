#pragma once
#ifndef FOUNDATION_CONCURRENT_DEQUE_HPP
#define FOUNDATION_CONCURRENT_DEQUE_HPP
#include <type_traits>
#include <memory>
#include <optional>
#include <new>
#include <atomic>

/**
 * Concurrent deque for our work-stealing taskgraph system based on the following implementations:
 * https://github.com/ConorWilliams/ConcurrentDeque
 * https://github.com/taskflow/work-stealing-queue
 * as mentioned by
 * https://blog.molecular-matters.com/2015/08/24/job-system-2-0-lock-free-work-stealing-part-1-basics/
 * 
 * all implementing the following papers:
 * "Dynamic Circular Work-Stealing Deque" https://www.dre.vanderbilt.edu/~schmidt/PDF/work-stealing-dequeue.pdf
 * "Correct and Efficient Work-Stealing for Weak Memory Models" https://inria.hal.science/hal-00802885/document
 *  - mostly targeted at ARM and PowerPC architectures, but still contains insights that are generally applicable
 * These are fantastic papers and this is an interesting topic, reading them is worth it
 */
namespace detail
{

    // In order to work ideally, ConcurrentDeque should only contain elements that satisfy the ConcurrentDequeElement concept.
    // key for this is nothrow construction, copying, and moving
    template<typename T>
    concept ConcurrentDequeElement = std::is_nothrow_default_constructible_v<T> &&
                                     std::is_nothrow_move_assignable_v<T> &&
                                     std::is_nothrow_move_constructible_v<T> &&
                                     std::is_nothrow_destructible_v<T>;

    template<ConcurrentDequeElement T>
    struct ConcurrentDequeRingBuffer
    {
        explicit ConcurrentDequeRingBuffer(size_t _capacity) noexcept : capacity{ _capacity }, mask{ _capacity - 1 }
        {
            // capacity must be power of 2
            assert((capacity & (capacity - 1)) == 0);
        }

        constexpr size_t Capacity() const noexcept
        {
            return capacity;
        }

        void Store(const size_t index, T&& value) noexcept
        {
            data[index & mask] = std::move(value);
        }

        T Load(const size_t index) const noexcept
        {
            return data[index & mask];
        }

        ConcurrentDequeRingBuffer<T>* Resize(const size_t beginRange, const size_t endRange) noexcept
        {
            // Create a new buffer with the new size
            auto newBuffer = new ConcurrentDequeRingBuffer<T>{ 2u * capacity };

            // Move elements from the old buffer to the new buffer
            for (size_t i = beginRange; i < endRange; ++i)
            {
                newBuffer->Store(i - beginRange, Load(i));
            }

            return newBuffer;
        }

    private:
        size_t capacity;
        size_t mask; // mask is initialized as capacity - 1, and since capacity is power of 2 we can modulo by just &'ing the mask with indices
        std::unique_ptr<T[]> data = std::make_unique_for_overwrite<T[]>(capacity);
    };

}

/**
 * @brief Dead-simple concurrent deque implementation. Owner thread uses LIFO ordering, while worker threads use FIFO to avoid stomping on the owner thread's work.
 * Resident types must satisfy the ConcurrentDequeElement concept, since that gives us a ton of lovely optimizations, and we use the ring buffer above for storage.
 */
template<detail::ConcurrentDequeElement T>
struct ConcurrentDeque
{
    // remember that new is overriden project wide to pass through mimalloc
    explicit ConcurrentDeque(const size_t capacity) : head{ 0u }, tail{ 0u }, buffer{ new detail::ConcurrentDequeRingBuffer<T>{ capacity } }
    {
        oldBuffers.reserve(8u);
    }

    ~ConcurrentDeque()
    {
        delete buffer.load();
    }

    // no moving or copying allowed
    ConcurrentDeque(const ConcurrentDeque&) = delete;
    ConcurrentDeque(ConcurrentDeque&&) = delete;
    ConcurrentDeque& operator=(const ConcurrentDeque&) = delete;
    ConcurrentDeque& operator=(ConcurrentDeque&&) = delete;

    size_t Size() const noexcept
    {
        const size_t _tail = tail.load(std::memory_order_relaxed);
        const size_t _head = head.load(std::memory_order_relaxed);
        return _tail >= _head ? _tail - _head : 0u;
    }

    constexpr size_t Capacity() const noexcept
    {
        return buffer->Capacity();
    }

    bool Empty() const noexcept
    {
        const size_t _tail = tail.load(std::memory_order_relaxed);
        const size_t _head = head.load(std::memory_order_relaxed);
        return _tail <= _head;

    }

    template<typename...Args>
    void EmplaceBack(Args&&... args) noexcept
    {
        T newObject(std::forward<Args>(args)...);

        const size_t _tail = tail.load(std::memory_order_relaxed);
        // top needs acquire as it's where stealing threads will be working from, and they'll use acquire ordering when stealing too
        const size_t _head = head.load(std::memory_order_acquire);
        auto _buffer = buffer.load(std::memory_order_relaxed);
        const size_t buffer_capacity = _buffer->Capacity();

        // check if out of room for newest element
        if (buffer_capacity < (_tail - _head) + 1)
        {
            
            auto newBuffer = _buffer->Resize(_head, _tail);
            // because of the exchange, the local _buffer pointer we declared will reflect new value
            oldBuffers.emplace_back(std::exchange(_buffer, newBuffer));
            buffer.store(_buffer, std::memory_order_relaxed);
        }

        _buffer->Store(_tail, std::move(newObject));
        std::atomic_thread_fence(std::memory_order_release);
        tail.store(_tail + 1, std::memory_order_relaxed);
    }

    std::optional<T> TryPop() noexcept
    {
        const size_t _tail = tail.load(std::memory_order_relaxed) - 1;
        auto _buffer = buffer.load(std::memory_order_relaxed);
        // store back so that stealers will no longer steal from this index
        tail.store(_tail, std::memory_order_relaxed);
        // thread fence now provides ordering guarantees on previous relaxed accesses
        std::atomic_thread_fence(std::memory_order_seq_cst); // has to be seq_cst to have effect on x86-x64

        const size_t _head = head.load(std::memory_order_relaxed);
        if (_head <= _tail)
        {
            // if we're at the last item, we could be fighting over it with another thread so need to do a CAS
            if (_head == _tail)
            {
                if (!head.compare_exchange_strong(_head, _head + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
                {
                    // if we failed to increment head, it means another thread has already popped this item
                    tail.store(_tail + 1, std::memory_order_relaxed);
                    return std::nullopt;
                }

                tail.store(_tail + 1, std::memory_order_relaxed);
            }

            return _buffer->load(_tail);
        }
        else
        {
            _tail.store(_tail + 1, std::memory_order_relaxed);
            return std::nullopt; // no items to pop
        }
    }

    std::optional<T> Steal() noexcept
    {
        const size_t _head = head.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst); // ensure we see the latest tail value
        const size_t _tail = tail.load(std::memory_order_acquire);

        if (_head < _tail)
        {
            T stolenItem = buffer->load(_head);
            if (!_head.compare_exchange_strong(_head, _head + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
            {
                // if we failed to increment head, it means another thread has already stolen this item
                return std::nullopt;
            }

            return stolenItem;
        }

        return std::nullopt;
    }

private:
    alignas(std::hardware_destructive_interference_size) std::atomic<size_t> head{ 0u };
    alignas(std::hardware_destructive_interference_size) std::atomic<size_t> tail{ 0u };
    // needs to be a raw pointer so we can deal with this atomically
    alignas(std::hardware_destructive_interference_size) detail::ConcurrentDequeRingBuffer<T>* buffer;
    std::vector<detail::ConcurrentDequeRingBuffer<T>> oldBuffers;
};

#endif //!FOUNDATION_CONCURRENT_DEQUE_HPP
