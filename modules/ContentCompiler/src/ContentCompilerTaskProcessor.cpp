#include "ContentCompilerTaskProcessor.hpp"

constexpr size_t NumWorkerThreads = 8;

namespace ContentCompiler
{

    TaskProcessor::TaskProcessor()
    {
        StartWorkers();
    }

    TaskProcessor::~TaskProcessor()
    {

        taskDispatcherThread.request_stop();
        taskDispatcherThread.join();

    }

    void TaskProcessor::ScheduleTask(std::coroutine_handle<> handle)
    {
        taskQueue.push(handle);
    }

    void TaskProcessor::StartWorkers()
    {
        auto workerFunction = [this](std::stop_token stopToken, std::coroutine_handle<> handle)
        {
            if (!stopToken.stop_requested() && handle)
            {
                handle.resume();
            }
        };

        auto taskDispatcherFunction = [this](std::stop_token stopToken)
        {
            // in order to make sure we can respond to stop requests, we won't use the queues blocking pop()
            // function and will instead use try_pop() with a wait/yield interval
            while (!stopToken.stop_requested())
            {
                std::optional<std::coroutine_handle<>> taskOpt;
                if (taskOpt = taskQueue.try_pop(), taskOpt.has_value())
                {
                    taskOpt->resume();
                }
                else
                {
                    std::this_thread::yield();
                }
            }

        };

    }

}
