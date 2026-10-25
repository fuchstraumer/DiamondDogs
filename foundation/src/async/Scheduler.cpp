#include "async/Scheduler.hpp"
#include "async/Task.hpp"
#include "async/Timeline.hpp"
#include "async/TimelineSource.hpp"
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

Scheduler::Scheduler() noexcept
{
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
    // that never started are in `roots` too
    parked.clear();

    for (std::coroutine_handle<> root : roots)
    {
        root.destroy();
    }
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

void Scheduler::Tick()
{
    checkOwnerThread();
    ++tickCount;

    pollSources();
    resumeReachedFrames();
    startSpawnedRoots();
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

std::string Scheduler::DumpState() const
{
    std::string output;

    for (const ParkedFrame& frame : parked)
    {
        std::format_to(std::back_inserter(output), "parked: timeline {} target {} (value {}), {} ticks\n",
                       static_cast<const void*>(frame.Source), frame.TargetValue, frame.Source->Value(),
                       tickCount - frame.ParkedAtTick);

        for (const BasePromise* promise = frame.Waiter; promise != nullptr; promise = promise->Awaiting)
        {
            std::string description = symbolizer.DescribeFrame(promise->FrameAddress);
            std::format_to(std::back_inserter(output), "    frame {}\n", description);
        }
    }

    return output;
}

void Scheduler::checkOwnerThread() noexcept
{
    const std::thread::id current = std::this_thread::get_id();

    if (ownerThread == std::thread::id{})
    {
        ownerThread = current;
    }

    assert(ownerThread == current && "a Scheduler is used from one owner thread only");
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

}
