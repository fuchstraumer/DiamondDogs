#include <coroutine>
#include <expected>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <queue>
#include "utility/delegate.hpp"

template<typename T>
class Task
{
public:

    struct promise_type
    {
        std::coroutine_handle<> parentHandle;
        std::expected<T, std::exception_ptr> result;
        friend class Task<T>;
    };

    template<typename OtherType>
    void DependsOn(Task<OtherType>&& other)
    {
        // if we depend on a task, it's the parent handle of our current task
        handle.promise().parentHandle = other.handle;
    }

private:
    std::coroutine_handle<promise_type> handle;
    std::atomic<size_t> dependencyCount; // dependencies we will wait on
    std::vector<std::coroutine_handle<>> dependentTasks; // tasks that we will signal/resume on completion

};

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

Task<std::vector<std::uint32_t>> CreateShaders(const char* file_path)
{


}