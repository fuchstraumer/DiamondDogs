#pragma once
#ifndef TASK_GRAPH_CORE_TASK_HPP
#define TASK_GRAPH_CORE_TASK_HPP
#include "CoroutineAllocator.hpp"
#include "Unit.hpp"
#include "utility/delegate.hpp"
#include <type_traits>
#include <coroutine>
#include <stop_token>
#include <exception>
#include <expected>
#include <vector>

// Based on what I see in folly's coroutine implementation, it seems that we need to add __declspec(noinline) to the final_suspend_awaiter
// to avoid leakage of stack allocated variables into the coroutine frame as part of LTO
// I do not envy whoever had to diagnose and fix that one.
#ifdef _MSC_VER
#define FINAL_AWAITER_NOINLINE __declspec(noinline)
#else
#define FINAL_AWAITER_NOINLINE
#endif

#if defined(_MSC_VER)
    #define TASK_UNREACHABLE() __assume(0)
#elif defined(__GNUC__)
    #define TASK_UNREACHABLE() __builtin_unreachable()
#endif


namespace TaskGraph
{

    // We'll want to store an error or exception pointer 
    struct BasePromise
    {
        std::coroutine_handle<> Continuation;
        size_t DependencyCount{ 0u };
        class IScheduler* Scheduler;
    };

    namespace detail
    {
        template<typename T>
        concept IsPointer = std::is_pointer_v<T>;
    }

    template<typename PromiseType>
    concept ValidPromiseType = requires(PromiseType promise)
    {
        // all promises must derive from BasePromise type
        { promise } -> std::derived_from<BasePromise>;
        // promises should have a scheduler pointer that derives from IScheduler
        { promise.Scheduler } -> std::derived_from<IScheduler>;
        // promises should implement a Result type that is std::expected<ResultType, std::exception_ptr>
        { promise.Result } -> std::same_as<std::expected<typename PromiseType::value_type, std::exception_ptr>>;
        { promise.Continuation } -> std::convertible_to<std::coroutine_handle<>>;
    };

    // concept of an awaitable for us to test against, so that we know if something is awaitable (because if not,
    // it's something we should execute synchronously and immediately instead)
    template<typename T>
    concept Awaitable = requires(T&& awaitable)
    {
        { awaitable.await_ready() } -> std::convertible_to<bool>;
        { awaitable.await_suspend(std::coroutine_handle<>{}) };
        { awaitable.await_resume() };
    } || requires(T&& awaitable)
    {
        { operator co_await(std::forward<T>(awaitable)) } -> Awaitable;
    };

    enum class TaskType : uint8_t
    {
        Invalid,
        Immediate, // task that is executed immediately and synchronously
        Deferred,  // task that is deferred and will be executed later
    };

    namespace detail
    {

        struct FinalAwaiter
        {
            constexpr bool await_ready() noexcept
            {
                return false;
            }

            constexpr void await_resume() noexcept
            {
                TASK_UNREACHABLE();
            }

            template<ValidPromiseType Promise>
            FINAL_AWAITER_NOINLINE std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept
            {
                auto& promise = h.promise();
                // promise has an error in the Result type
                if (!promise.Result.has_value())
                {
                    // result is an exception, we need to forward that to the continuation so it can decide what to do
                    assert(false); // TODO: not implemented yet
                }
                else if (promise.Continuation)
                {
                    // result is fine, just return continuation
                    return promise.Continuation;
                }
                else
                {
                    return std::noop_coroutine();
                }
            }
        };

    }

    // Non-joinable task type that generates a result of type T when complete. Stored in a std::expected to allow for potential errors as well.
    template<typename T>
    class Task
    {
    public:

        struct promise_type : BasePromise
        {
            
            void* operator new(std::size_t size)
            {
                return CoroutineAllocator::Allocate(size);
            }

            void operator delete(void* ptr, std::size_t size)
            {
                CoroutineAllocator::Deallocate(ptr, size);
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
            detail::FinalAwaiter final_suspend() noexcept
            {
                return {};
            }

            void unhandled_exception()
            {
                result = std::unexpected(std::current_exception());
            }

            void return_value(T value)
            {
                // called by co_return with value T
                result = std::move(value);
            }

        private:
            std::expected<T, std::exception_ptr> result;
            friend class Task<T>;
        };

        Task(std::coroutine_handle<promise_type> handle) : handle(handle)
        {
        }

        // to link tasks together, we need to consider they may have alternate result types
        template<ValidPromiseType OtherPromiseType>
        void DependsOn(Task<OtherPromiseType>&& other)
        {
            other.promise.Continuation = handle;
        }

        T& GetResult() &
        {
            if (promise.result.has_value())
            {
                return promise.result.value();
            }
            else
            {
                std::rethrow_exception(promise.result.error());
            }
        }

        T&& GetResult() &&
        {
            if (promise.result.has_value())
            {
                return std::move(promise.result.value());
            }
            else
            {
                std::rethrow_exception(promise.result.error());
            }
        }

    private:
        std::coroutine_handle<promise_type> handle;
        std::stop_token stopToken;
    };

}

#endif // !CONTENT_COMPILER_TASK_HPP
