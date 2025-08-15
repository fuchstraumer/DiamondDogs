#pragma once
#ifndef TASK_GRAPH_CORE_TASK_HPP
#define TASK_GRAPH_CORE_TASK_HPP
#include "CoroutineAllocator.hpp"
#include <type_traits>
#include <coroutine>
#include <stop_token>
#include <exception>
#include <expected>

namespace TaskGraph
{
    // Non-joinable task type that generates a result of type T when complete. Stored in a std::expected to allow for potential errors as well.
    template<typename T>
    class Task
    {
    public:

        struct promise_type
        {
            // Node that this task is associated with
            NodeId associatedNodeId = 0;
            // to handle continuations and child tasks, we need a parent coroutine handle. we will decide if we call 
            // this or not during final_suspend 
            std::coroutine_handle<> parentHandle;

            void* operator new(std::size_t size)
            {
                return CoroutineAllocator::Allocate(size, associatedNodeId);
            }

            void operator delete(void* ptr, std::size_t size)
            {
                CoroutineAllocator::Deallocate(ptr, size, associatedNodeId);
            }

            Task<T> get_return_object()
            {
                return Task<T>{ std::coroutine_handle<promise_type>::from_promise(*this) };
            }

            // Always suspend, until we can ensure dependencies are met and fully scheduled
            std::suspend_always initial_suspend() noexcept
            {
                return {};
            }

            // always suspend during final suspend so we can handle dependencies and listeners
            std::suspend_always final_suspend() noexcept
            {
                return {};
            }

            void unhandled_exception()
            {
                exceptionPtr = std::current_exception();
            }

            void return_value(T value)
            {
                // called by co_return with value T
                result = std::move(value);
            }

        private:
            std::expected<T> result;
            std::exception_ptr exceptionPtr;
            friend class Task<T>;
        };

        // to link tasks together, we need to consider they may have alternate result types
        template<typename OtherType>
        void DependsOn(Task<OtherType>&& other)
        {
            // Link this task to the other task
            other.promise.parentHandle = std::coroutine_handle<promise_type>::from_promise(*this);
        }

    private:
        std::coroutine_handle<promise_type> handle;
        std::stop_token stopToken;
    };

}

#endif // !CONTENT_COMPILER_TASK_HPP
