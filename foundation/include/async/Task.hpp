#pragma once
#ifndef FOUNDATION_ASYNC_TASK_HPP
#define FOUNDATION_ASYNC_TASK_HPP
#include "async/Promise.hpp"
#include "Unit.hpp"
#include <coroutine>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

namespace foundation
{

class Scheduler;

// An awaitable that needs the scheduler of the coroutine that awaits it + it's awaiting promise
// to help construct readable traces and graphs
template<typename Awaiter>
concept SchedulerBound = requires(std::remove_reference_t<Awaiter>& awaiter, Scheduler* owner, BasePromise* awaiting)
{
    awaiter.BindOwner(owner, awaiting);
};

namespace detail
{
    template<typename T>
    using StoredType = std::conditional_t<std::is_void_v<T>, Unit, T>;

    // even with Unit, we still have to split because a promise can only declare
    // return_value or return void, since the compiler's transform for coroutines
    // is effectively a name lookup

    template<typename T>
    struct PromiseReturn
    {
        std::optional<StoredType<T>> Value;
        template<typename U>
        void return_value(U&& value)
        {
            Value.emplace(std::forward<U>(value));
        }
    };

    template<>
    struct PromiseReturn<void>
    {
        std::optional<Unit> Value;

        void return_void() noexcept
        {
            Value.emplace(k_Unit);
        }
    };

}

/**@brief Lazily-executing coroutine. Nothing runs until it is awaited or spawned, and it
 * owns it's coroutine frame. */
template<typename T>
class [[nodiscard]] Task
{
public:
    struct promise_type final : BasePromise, detail::PromiseReturn<T>
    {
        Task get_return_object() noexcept;
        [[nodiscard]] constexpr std::suspend_always initial_suspend() const noexcept
        {
            return {};
        }

        FinalAwaiter final_suspend() noexcept;
        void unhandled_exception() noexcept;
        
        template<typename Awaiter>
        Awaiter&& await_transform(Awaiter&& awaiter) noexcept
        {
            if constexpr (SchedulerBound<Awaiter>)
            {
                // passing `this` is how we keep the callstack intact for tracing/debugging
                awaiter.BindOwner(Owner, static_cast<BasePromise*>(this));
            }

            return std::forward<Awaiter>(awaiter);
        }

    };

    struct Awaiter
    {
        std::coroutine_handle<promise_type> Child;
        [[nodiscard]] bool await_ready() const noexcept;
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) noexcept;
        T await_resume() noexcept;
    };

    explicit Task(std::coroutine_handle<promise_type> _handle) noexcept;
    Task(Task&& other) noexcept;
    Task(const Task&) = delete; // disable copy constructor
    Task& operator=(Task&& other) noexcept;
    Task& operator=(const Task&) = delete; // disable copy assignment
    ~Task();

    void BindOwner(Scheduler* owner, BasePromise* awaiting) noexcept; // what makes SchedulerBound true/usable
    Awaiter operator co_await() && noexcept;
private:
    friend class Scheduler;
    std::coroutine_handle<promise_type> handle;
};


template<typename T>
Task<T> Task<T>::promise_type::get_return_object() noexcept
{
    auto resultHandle =
        std::coroutine_handle<promise_type>::from_promise(*this);
    FrameAddress = resultHandle.address();
    return Task<T>{ resultHandle };
}

template<typename T>
FinalAwaiter Task<T>::promise_type::final_suspend() noexcept
{
    return FinalAwaiter{};
}

template<typename T>
void Task<T>::promise_type::unhandled_exception() noexcept
{
    std::terminate();
}

template<typename T>
bool Task<T>::Awaiter::await_ready() const noexcept
{
    return false; // Awaiter is never ready immediately
}

template<typename T>
std::coroutine_handle<> Task<T>::Awaiter::await_suspend(std::coroutine_handle<> parent) noexcept
{
    Child.promise().Continuation = parent;
    return Child;
}

template<typename T>
T Task<T>::Awaiter::await_resume() noexcept
{
    if constexpr (std::is_same_v<T, void>)
    {
        return;
    }
    else
    {
        return std::move(*Child.promise().Value);
    }
}

template<typename T>
Task<T>::Task(std::coroutine_handle<promise_type> _handle) noexcept : handle(_handle)
{
}

template<typename T>
Task<T>::Task(Task&& other) noexcept
    : handle(std::exchange(other.handle, {}))
{
}

template<typename T>
Task<T>& Task<T>::operator=(Task&& other) noexcept
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

template<typename T>
Task<T>::~Task()
{
    // we will handle cancellation later
    if (handle)
    {
        handle.promise().Owner = nullptr;
        handle.promise().Awaiting = nullptr;
        handle.destroy();
    }
}

template<typename T>
void Task<T>::BindOwner(Scheduler* owner, BasePromise* awaiting) noexcept
{
    promise_type& promise = handle.promise();
    promise.Owner = owner;
    promise.Awaiting = awaiting;
}

template<typename T>
typename Task<T>::Awaiter Task<T>::operator co_await() && noexcept
{
    // the Task stays alive in the awaiting frame's co_await expression, so it still destroys the child frame
    return Awaiter{ handle };
}

} // namespace foundation

#endif // !FOUNDATION_ASYNC_TASK_HPP
