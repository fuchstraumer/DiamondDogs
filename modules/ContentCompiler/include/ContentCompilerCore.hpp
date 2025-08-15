#pragma once
#ifndef CONTENT_COMPILER_CORE_HPP
#define CONTENT_COMPILER_CORE_HPP
#include "ContentCompilerTypes.hpp"
#include <coroutine>
#include <exception>
#include <atomic>
#include <memory>
#include <expected>

namespace ContentCompiler
{

// Custom coroutine task type
template<typename T>
class Task
{
public:
    struct promise_type
    {
        NodeId associatedNodeId = 0;
        
        void* operator new(std::size_t size)
        {
            auto nodeId = getCurrentNodeId();
            return CoroutineAllocator::allocate(size, nodeId);
        }
        
        void operator delete(void* ptr, std::size_t size)
        {
            auto nodeId = getCurrentNodeId();
            CoroutineAllocator::deallocate(ptr, size, nodeId);
        }
        
        Task<T> get_return_object()
        {
            return Task<T>{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        
        // We can suspend immediately upon entering this coroutine, but by returning suspend_never
        // we choose to never do this.
        std::suspend_never initial_suspend() noexcept { return {}; }

        // coroutine is about to complete, either by reaching co_return or exiting the coroutine body. 
        // this awaiter handles what we do next
        auto final_suspend() noexcept
        { 
            return {};
        }
        
        void unhandled_exception()
        {
            exception = std::current_exception();
        }
        
        void return_value(T value) requires std::is_object_v<T>
        {
            result = std::move(value);
        }
        
        // Context for memory tracking
        static thread_local NodeId currentNodeId;

        static NodeId getCurrentNodeId()
        {
            return currentNodeId;
        }

        static void setCurrentNodeId(NodeId id)
        {
            currentNodeId = id;
        }
        
    private:
        std::expected<T> result;
        std::exception_ptr exception;
        
        friend class Task<T>;
    };
    
    // Task interface
    explicit Task(std::coroutine_handle<promise_type> h) : handle(h) {}
    
    ~Task()
    {
        if (handle)
        {
            handle.destroy();
        }
    }
    
    // Move-only semantics
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    
    Task(Task&& other) noexcept : handle(std::exchange(other.handle, {})) {}

    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            if (handle)
            {
                handle.destroy();
            }
            handle = std::exchange(other.handle, {});
        }
        return *this;
    }
    
    bool is_ready() const noexcept
    {
        return handle && handle.done();
    }

    T get() requires (!std::is_void_v<T>)
    {
        if (!handle)
        {
            throw std::runtime_error("Task handle is null");
        }
        
        if (handle.promise().exception)
        {
            std::rethrow_exception(handle.promise().exception);
        }

        if (!handle.promise().result.has_value())
        {
            throw std::runtime_error("Task completed without result");
        }
        
        return std::move(*handle.promise().result);
    }
    
    void get() requires std::is_void_v<T>
    {
        if (!handle)
        {
            throw std::runtime_error("Task handle is null");
        }
        
        if (handle.promise().exception)
        {
            std::rethrow_exception(handle.promise().exception);
        }
    }
    
    void wait()
    {
        // For now, simple busy wait - we'll improve this with proper awaiting
        while (handle && !handle.done())
        {
            std::this_thread::yield();
        }
    }
    
    bool wait_for(std::chrono::milliseconds timeout)
    {
        auto start = std::chrono::steady_clock::now();
        while (handle && !handle.done())
        {
            if (std::chrono::steady_clock::now() - start > timeout)
            {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }
    
    // Set the node context for memory tracking
    void setNodeContext(NodeId nodeId)
    {
        if (handle)
        {
            handle.promise().associatedNodeId = nodeId;
            promise_type::setCurrentNodeId(nodeId);
        }
    }
    
private:
    std::coroutine_handle<promise_type> handle;
};


// Void return specialization of the task type above
template<>
class Task<void>
{

};

// Class that non-coroutine callers can await() on. Signals a condition variable from final_suspend
template<typename T>
class JoinableTask
{

};

// Void specialization of joinable task
template<>
class JoinableTask<void>
{

};

// Thread-local storage for current node context
template<typename T>
thread_local NodeId Task<T>::promise_type::currentNodeId = 0;

// Atomic delegate for thread-safe callbacks without mutexes
template<typename... Args>
class AtomicDelegate
{
private:
    // We store a pointer to delegate to avoid copying issues
    std::atomic<delegate_t<void(Args...)>*> delegate{nullptr};
    
public:
    AtomicDelegate() = default;
    
    void set(delegate_t<void(Args...)>* del)
    {
        delegate.store(del, std::memory_order_release);
    }

    void operator()(Args... args) const
    {
        auto* del = delegate.load(std::memory_order_acquire);
        if (del && !del->empty())
        {
            (*del)(args...);
        }
    }

    bool empty() const noexcept
    {
        auto* del = delegate.load(std::memory_order_acquire);
        return !del || del->empty();
    }
    
    void clear()
    {
        delegate.store(nullptr, std::memory_order_release);
    }
};

} // namespace ContentCompiler

#endif // !CONTENT_COMPILER_CORE_HPP
