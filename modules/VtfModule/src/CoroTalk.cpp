#include <coroutine>
#include <expected>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <queue>
#include "threading/SrwLock.hpp"
#include "utility/delegate.hpp"

struct GraphicsPipelineCreateInfo
{
    uint32_t someStateBits;
    uint32_t someMoreStateBits;
    uint64_t simulatedPointerToStateData;
    bool operator==(const GraphicsPipelineCreateInfo& other) const
    {
        return someStateBits == other.someStateBits &&
               someMoreStateBits == other.someMoreStateBits &&
               simulatedPointerToStateData == other.simulatedPointerToStateData;
    }
};

struct VulkanPipeline
{
    std::string name;
    GraphicsPipelineCreateInfo createInfo;
    bool operator==(const VulkanPipeline& other) const
    {
        return name == other.name && createInfo == other.createInfo;
    }
};

SrwLock pipelineCacheLock;
std::unordered_map<std::string, VulkanPipeline> pipelineCache;


class TaskScheduler
{
public:

    void Schedule(std::coroutine_handle<> task)
    {
        // Add the task to the queue for execution
        taskQueue.push(task);
    }

private:
    std::queue<std::coroutine_handle<>> taskQueue;
};

struct IoData
{
    std::vector<char> someIoData;
};

struct IoResult
{
    IoData data;
    static IoResult FromData(IoData&& data)
    {
        IoResult result;
        result.data = std::move(data);
        return result;
    }
};

struct Scheduler
{

};

struct BasePromise
{
    BasePromise* Child = nullptr;
    BasePromise* Caller = nullptr;
    std::coroutine_handle<> ParentHandle;
    std::coroutine_handle<> ChildHandle;
    Scheduler* SchedulerPtr = nullptr;
    virtual ~BasePromise() = default;
    void SetScheduler(Scheduler* scheduler) noexcept
    {
        SchedulerPtr = scheduler;
        if (Child)
        {
            Child->SetScheduler(scheduler);
        }
    }
};

namespace detail
{
    template<class T>
    concept IsPointerConcept = std::is_pointer_v<T>;
}

template<typename T>
concept BasePromiseType = requires(T t)
{
    { t } -> std::derived_from<BasePromise>;
    { t.Scheduler } -> detail::IsPointerConcept;
};

template<typename T>
struct Async
{
    // symmetric transferer implementation/object for traversing out of nested coroutines during final-suspend
    struct ResumeCaller
    {
        constexpr bool await_ready() noexcept { return false; }
        void await_resume() noexcept {}
        std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept
        {
            if (handle.promise().Caller)
            {
                return std::coroutine_handle<>::from_address(handle.promise().Caller);
            }
            else
            {
                return std::noop_coroutine();
            }
        }
    };

    struct promise_type : BasePromise
    {
        std::suspend_always initial_suspend() noexcept { return {}; }

        std::suspend_always final_suspend() noexcept
        {
            return ResumeCaller{};
        }

        void unhandled_exception() noexcept { std::terminate(); }

        Async<T> get_return_object() noexcept
        {
            auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
            return Async<T>{ handle };
        }

        void return_value(T value) noexcept { data = std::move(value); }

        T data;
        friend struct Async<T>;
        // this is the coroutine handle of any coroutines that may have co_await'd *this* particular Async object
        // this is set by await_suspend(), which is called with the handle to the coroutine currently executing. *this* particular
        // object is suspended and asleep, but we're now in possession of the handle to our "parent" coroutine
        // this way we can traverse the stack upwards as we complete and ensure things are resumed.
        
        Scheduler* scheduler = nullptr; // pointer to the scheduler that manages this coroutine
    };

    std::coroutine_handle<promise_type> self;

    Async(std::coroutine_handle<promise_type> h) : self(h) {}

    bool await_ready() const noexcept { return false; }
    T await_resume() { return std::move(self.promise().data); }

    // need to standardize CallerPromiseType to be a concept that has required members
    template<BasePromiseType CallerPromiseType>
    auto await_suspend(std::coroutine_handle<CallerPromiseType> caller) noexcept
    {
        caller.promise().Child = &self.promise();
        self.promise().Caller = &caller.promise();
        return self;
    }
};

Async<IoResult> inner_function()
{
    struct async_io
    {
        using promise_type = Async<IoResult>::promise_type;
        bool await_ready() { return false; }

        void await_suspend(std::coroutine_handle<promise_type> coro)
        {
            handle = coro;
        }

        IoData await_resume()
        {
            // Simulate some IO operation
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return IoData{ std::vector<char>{'d', 'a', 't', 'a'} };
        }

        std::coroutine_handle<promise_type> handle;
    };
    // this awaitable is the one that is doing the actual reading for us
    auto data = co_await async_io{};
    // this returns the data previously read
    co_return IoResult::FromData(std::move(data));
}

struct PartialResult
{

};

Async<PartialResult> middle_function()
{
    Async<IoResult> io_awaitable = inner_function();
    IoResult io_result = co_await io_awaitable;
    // Process the IO result and create a partial result
    PartialResult partial_result;
    // ...
    co_return partial_result;
}

struct FinalResult
{

};

Async<FinalResult> outer_function()
{
    Async<PartialResult> partial_awaitable = middle_function();
    PartialResult partial_result = co_await partial_awaitable;
    // Process the partial result and create a final result
    FinalResult final_result;
    // ...
    co_return final_result;
}

int main(int argc, char* argv[])
{
    auto async_operation = outer_function();
    // .... pretend time elapses
    async_operation.resume_function();
    FinalResult result = async_operation.get_result();
}
