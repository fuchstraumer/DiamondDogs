#include "LifetimeTracked.hpp"

namespace containers
{

int64_t LifetimeTracked::Live{ 0 };
int64_t LifetimeTracked::Faults{ 0 };

void LifetimeTracked::ResetCounters() noexcept
{
    Live = 0;
    Faults = 0;
}

LifetimeTracked::LifetimeTracked() noexcept :
    LifetimeTracked(0)
{
}

LifetimeTracked::LifetimeTracked(int32_t _value) noexcept :
    state{ State::Alive },
    value{ _value }
{
    ++Live;
}

LifetimeTracked::LifetimeTracked(const LifetimeTracked& other) noexcept :
    state{ State::Alive },
    value{ other.value }
{
    readSource(other);
    ++Live;
}

LifetimeTracked::LifetimeTracked(LifetimeTracked&& other) noexcept :
    state{ State::Alive },
    value{ other.value }
{
    readSource(other);
    other.state = State::MovedFrom;
    ++Live;
}

LifetimeTracked& LifetimeTracked::operator=(const LifetimeTracked& other) noexcept
{
    checkAssignmentTarget();
    readSource(other);
    value = other.value;
    state = State::Alive;
    return *this;
}

LifetimeTracked& LifetimeTracked::operator=(LifetimeTracked&& other) noexcept
{
    checkAssignmentTarget();
    readSource(other);
    value = other.value;
    state = State::Alive;
    other.state = State::MovedFrom;
    return *this;
}

LifetimeTracked::~LifetimeTracked()
{
    // a moved-from object is still alive as far as C++ is concerned, so destroying it is fine
    if (state != State::Alive && state != State::MovedFrom)
    {
        ++Faults;
    }

    state = State::Destroyed;
    --Live;
}

int32_t LifetimeTracked::Value() const noexcept
{
    if (state != State::Alive)
    {
        ++Faults;
    }

    return value;
}

void LifetimeTracked::readSource(const LifetimeTracked& source) const noexcept
{
    // a container only copies or moves from elements that hold a value. a moved-from source means it read an
    // element that it had already moved, which is the aliasing bug these tests look for
    if (source.state != State::Alive)
    {
        ++Faults;
    }
}

void LifetimeTracked::checkAssignmentTarget() const noexcept
{
    if (state != State::Alive && state != State::MovedFrom)
    {
        ++Faults;
    }
}

}
