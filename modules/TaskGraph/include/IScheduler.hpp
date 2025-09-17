#pragma once
#ifndef TASK_GRAPH_TASK_SCHEDULER_INTERFACE_HPP
#define TASK_GRAPH_TASK_SCHEDULER_INTERFACE_HPP
#include <coroutine>

struct BasePromise;

/**
 * @brief Defines the abstract interface required for task scheduling, both for CPU and GPU workloads.
 */
class IScheduler
{
public:
    virtual ~IScheduler() = default;

    virtual void Schedule(std::coroutine_handle<BasePromise> handle) noexcept = 0;
    
    /** @brief Scheduler this task for execution after the timeline semaphore reaches given value, not relative to a task
     * To schedule tasks after or before each other, use the interface on the task object
     */
    virtual void ScheduleAfter(std::coroutine_handle<BasePromise> handle, uint64_t timelineSemaphoreValue) noexcept = 0;

    virtual bool CanExecute(const BasePromise& promise) const noexcept = 0;

};

#endif // !TASK_GRAPH_TASK_SCHEDULER_INTERFACE_HPP
