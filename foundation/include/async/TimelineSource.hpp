#pragma once
#ifndef FOUNDATION_ASYNC_TIMELINE_SOURCE_HPP
#define FOUNDATION_ASYNC_TIMELINE_SOURCE_HPP

namespace foundation
{

/**@brief Something outside the scheduler that advances timelines, for example a GPU queue. The Scheduler calls
 * Poll on its owner thread at the start of every Tick, before it resumes any parked frame. A Poll reads the
 * outside state and calls Timeline::Signal. It never blocks. Implementations should be final. */
class TimelineSource
{
public:
    TimelineSource() = default;
    virtual ~TimelineSource() = default;
    // the scheduler holds the address of a source
    TimelineSource(const TimelineSource&) = delete;
    TimelineSource& operator=(const TimelineSource&) = delete;
    TimelineSource(TimelineSource&&) = delete;
    TimelineSource& operator=(TimelineSource&&) = delete;

    virtual void Poll() = 0;
};

}

#endif // !FOUNDATION_ASYNC_TIMELINE_SOURCE_HPP
