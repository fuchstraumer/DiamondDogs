#include "async/Scheduler.hpp"
#include "async/FrameHeap.hpp"
#include "async/Task.hpp"
#include "async/Timeline.hpp"
#include "async/TimelineSource.hpp"
#include "async/Yield.hpp"
#include <algorithm>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <cstdio>
#include <format>
#include <iterator>
#include <print>
#include <string>
#include <thread>
#include <utility>

namespace foundation
{

Scheduler::Scheduler() noexcept : ownerThread{ std::this_thread::get_id() }
{
    // the first root's frame is created before Spawn, so the heap is bound here and not on the first call
    frameHeap.BindToCurrentThread();
}

Scheduler::~Scheduler()
{
#ifndef NDEBUG
    if (!roots.empty())
    {
        std::println(stderr, "[async] Scheduler destroyed with {} live roots and {} parked frames:\n{}", roots.size(),
                     parked.size(), DumpState());
    }
#endif

    // a root's frame owns the Tasks it is awaiting, so destroying the roots destroys every frame below them. Roots
    // that never started are in `roots` too. Ready and yielded frames belong to a root as well
    parked.clear();
    ready.clear();
    yielded.clear();
    yieldedPreviously.clear();

    for (std::coroutine_handle<> root : roots)
    {
        root.destroy();
    }

    frameHeap.UnbindFromCurrentThread();
}

void Scheduler::Spawn(Task<void>&& root)
{
    checkOwnerThread();
    std::coroutine_handle<Task<void>::promise_type> handle = std::exchange(root.handle, nullptr);
    assert(handle);

    Task<void>::promise_type& promise = handle.promise();
    promise.Owner = this;
    promise.Awaiting = nullptr;

    roots.push_back(handle);
    spawned.push_back(handle);
}

void Scheduler::Park(const Timeline& source, uint64_t target_value, std::coroutine_handle<> handle, BasePromise* waiter)
{
    // a frame parks while it runs, and frames only run on the owner thread: this catches a resume from anywhere else
    checkOwnerThread();
    parked.push_back(ParkedFrame{ &source, target_value, handle, waiter, tickCount });
}

void Scheduler::ParkUntilNextTick(std::coroutine_handle<> handle, BasePromise* waiter)
{
    checkOwnerThread();
    yielded.push_back(ReadyFrame{ handle, waiter, tickCount });
}

void Scheduler::Post(std::coroutine_handle<> handle)
{
    Post(handle, nullptr);
}

void Scheduler::Post(std::coroutine_handle<> handle, BasePromise* waiter)
{
    checkOwnerThread();
    ready.push_back(ReadyFrame{ handle, waiter, tickCount });
}

void Scheduler::Tick()
{
    checkOwnerThread();
    ++tickCount;

    // taken before any frame runs: a frame that yields in this Tick lands in `yielded` and waits for the next one
    std::swap(yieldedPreviously, yielded);

    pollSources();
    resumeReachedFrames();
    startSpawnedRoots();
    runReadyList();
    destroyFinishedRoots();
}

void Scheduler::AddSource(TimelineSource& source)
{
    checkOwnerThread();
    assert(std::ranges::find(sources, &source) == sources.end() && "a source is added once");

    sources.push_back(&source);
}

void Scheduler::RemoveSource(TimelineSource& source)
{
    checkOwnerThread();

    const auto removed = std::ranges::remove(sources, &source);
    assert(!removed.empty() && "RemoveSource needs a registered source");

    sources.erase(removed.begin(), removed.end());
}

bool Scheduler::Idle() const noexcept
{
    return roots.empty();
}

int32_t Scheduler::RootCount() const noexcept
{
    return static_cast<int32_t>(roots.size());
}

int32_t Scheduler::ParkedCount() const noexcept
{
    return static_cast<int32_t>(parked.size());
}

int64_t Scheduler::TickCount() const noexcept
{
    return tickCount;
}

FrameStatisticsSnapshot Scheduler::FrameStatistics() const noexcept
{
    return frameHeap.Snapshot();
}

std::string Scheduler::DumpState() const
{
    std::string output;

    for (const ParkedFrame& frame : parked)
    {
        std::format_to(std::back_inserter(output), "parked: timeline {} target {} (value {}), {} ticks\n",
                       static_cast<const void*>(frame.Source), frame.TargetValue, frame.Source->Value(),
                       tickCount - frame.ParkedAtTick);
        appendBacktrace(output, frame.Waiter, frame.Handle);
    }

    appendReadyFrames(output, "ready", ready);
    appendReadyFrames(output, "yielded", yielded);

    return output;
}

void Scheduler::checkOwnerThread() const noexcept
{
    assert(ownerThread == std::this_thread::get_id() && "a Scheduler is used from its owner thread only");
}

void Scheduler::pollSources()
{
    for (TimelineSource* source : sources)
    {
        source->Poll();
    }
}

void Scheduler::startSpawnedRoots()
{
    // a starting root can spawn another root, so take the list before resuming any of them
    starting.clear();
    std::swap(starting, spawned);

    for (std::coroutine_handle<> root : starting)
    {
        root.resume();
    }
}

void Scheduler::resumeReachedFrames()
{
    // collect first: a resumed frame can park again, which appends to `parked`
    resuming.clear();
    std::erase_if(parked,
                  [this](const ParkedFrame& frame)
                  {
                      if (!frame.Source->Reached(frame.TargetValue))
                      {
                          return false;
                      }

                      resuming.push_back(frame);
                      return true;
                  });

    for (const ParkedFrame& frame : resuming)
    {
        frame.Handle.resume();
    }
}

void Scheduler::runReadyList()
{
    // a resumed frame can yield again, which appends to `yielded`, and it can post, which appends to `ready`
    for (const ReadyFrame& frame : yieldedPreviously)
    {
        frame.Handle.resume();
    }

    yieldedPreviously.clear();

    while (!ready.empty())
    {
        draining.clear();
        std::swap(draining, ready);

        for (const ReadyFrame& frame : draining)
        {
            frame.Handle.resume();
        }
    }
}

void Scheduler::destroyFinishedRoots()
{
    std::erase_if(roots,
                  [](std::coroutine_handle<> root)
                  {
                      if (!root.done())
                      {
                          return false;
                      }

                      root.destroy();
                      return true;
                  });
}

void Scheduler::appendReadyFrames(std::string& output, const char* label, const std::vector<ReadyFrame>& frames) const
{
    for (const ReadyFrame& frame : frames)
    {
        std::format_to(std::back_inserter(output), "{}: {} ticks\n", label, tickCount - frame.QueuedAtTick);
        appendBacktrace(output, frame.Waiter, frame.Handle);
    }
}

void Scheduler::appendBacktrace(std::string& output, const BasePromise* waiter, std::coroutine_handle<> handle) const
{
    // a frame that was posted by handle only has no promise to walk, so it prints its own frame
    if (waiter == nullptr)
    {
        std::string description = symbolizer.DescribeFrame(handle.address());
        std::format_to(std::back_inserter(output), "    frame {}\n", description);
        return;
    }

    for (const BasePromise* promise = waiter; promise != nullptr; promise = promise->Awaiting)
    {
        std::string description = symbolizer.DescribeFrame(promise->FrameAddress);
        std::format_to(std::back_inserter(output), "    frame {}\n", description);
    }
}

bool YieldAwaiter::await_ready() const noexcept
{
    return false;
}

void YieldAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept
{
    // Owner is null when the awaiting coroutine is not a Task: its promise has no await_transform to bind us
    assert(Owner != nullptr);
    Owner->ParkUntilNextTick(handle, Waiter);
}

void YieldAwaiter::await_resume() const noexcept
{
}

void YieldAwaiter::BindOwner(Scheduler* scheduler, BasePromise* awaiting) noexcept
{
    Owner = scheduler;
    Waiter = awaiting;
}

YieldAwaiter Yield() noexcept
{
    return YieldAwaiter{};
}

}
