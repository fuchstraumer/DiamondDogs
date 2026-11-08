#pragma once
#ifndef TESTS_FOUNDATION_CONTAINERS_LIFETIME_TRACKED_HPP
#define TESTS_FOUNDATION_CONTAINERS_LIFETIME_TRACKED_HPP
#include <cstdint>

namespace containers
{

/** @brief A test element that checks its own lifetime. It records a fault when a container reads it after its lifetime
 * ended, assigns to it before its lifetime began, or destroys it twice. Live counts the instances that exist, so a
 * leak or a double destroy shows as a count that differs from the container's size. */
class LifetimeTracked
{
public:
    static int64_t Live;
    static int64_t Faults;
    static void ResetCounters() noexcept;

    LifetimeTracked() noexcept;
    // implicit on purpose, so that a range of int32_t converts into a container of LifetimeTracked
    LifetimeTracked(int32_t _value) noexcept;
    LifetimeTracked(const LifetimeTracked& other) noexcept;
    LifetimeTracked(LifetimeTracked&& other) noexcept;
    LifetimeTracked& operator=(const LifetimeTracked& other) noexcept;
    LifetimeTracked& operator=(LifetimeTracked&& other) noexcept;
    ~LifetimeTracked();

    /** @brief Returns the value, and records a fault when this object is not alive. */
    [[nodiscard]] int32_t Value() const noexcept;

private:
    // magic values, so that raw or freed memory is very unlikely to read as a live object
    enum class State : uint32_t
    {
        Invalid = 0,
        Alive = 0xA11CE5u,
        MovedFrom = 0x30BED0u,
        Destroyed = 0xDEAD00u,
    };

    void readSource(const LifetimeTracked& source) const noexcept;
    void checkAssignmentTarget() const noexcept;

    State state{ State::Invalid };
    int32_t value{ 0 };
};

}

#endif // !TESTS_FOUNDATION_CONTAINERS_LIFETIME_TRACKED_HPP
