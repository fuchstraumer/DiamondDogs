#pragma once
#ifndef CONTENT_COMPILER_TASK_PROCESSOR_HPP
#define CONTENT_COMPILER_TASK_PROCESSOR_HPP
#include "ContentCompilerCore.hpp"
#include "ContentCompilerTask.hpp"
#include "containers/mwsrQueue.hpp"
#include <thread>
#include <vector>

namespace ContentCompiler
{

    class TaskProcessor
    {
    public:

        static TaskProcessor& Get()
        {
            static TaskProcessor instance;
            return instance;
        }

        void ScheduleTask(std::coroutine_handle<> handle);

    private:
        TaskProcessor();
        ~TaskProcessor();

        TaskProcessor(const TaskProcessor&) = delete;
        TaskProcessor& operator=(const TaskProcessor&) = delete;

        mwsrQueue<std::coroutine_handle<>> taskQueue;
        // one thread that just reads tasks from the queue
        std::jthread taskDispatcherThread;

        void StartWorkers();

    };

}

#endif // !CONTENT_COMPILER_TASK_PROCESSOR_HPP
