#pragma once
#ifndef FOUNDATION_ASYNC_PROMISE_HPP
#define FOUNDATION_ASYNC_PROMISE_HPP
#include <coroutine>
#include <type_traits>
#include <utility>

#ifdef _MSC_VER
#define FINAL_AWAITER_NOINLINE __declspec(noinline)
#else
#define FINAL_AWAITER_NOINLINE [[gnu::noinline]]
#endif // _MSC_VER

namespace foundation
{

    class Scheduler;

    struct BasePromise
    {
        Scheduler* Owner{ nullptr };
        std::coroutine_handle<> Continuation{ nullptr };
    };

    template<typename PromiseType>
    concept ValidPromiseType = std::is_base_of_v<BasePromise, PromiseType>;

    // base type for continuations
    struct FinalAwaiter
    {

        [[nodiscard]] constexpr bool await_ready() const noexcept
        {
            return false; // FinalAwaiter is never ready immediately
        }

        template<ValidPromiseType PromiseType>
        FINAL_AWAITER_NOINLINE std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseType> handle)
        {
            auto& promise = handle.promise();
            if (promise.Continuation)
            {
                return promise.Continuation;
            }
            else
            {
                return std::noop_coroutine();
            }
        }

        [[noreturn]] constexpr void await_resume() const noexcept
        {
            std::unreachable();
        }
    };

}

#endif // !FOUNDATION_ASYNC_PROMISE_HPP
