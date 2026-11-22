#include "timing/HighResolutionWait.hpp"
#include <cerrno>
#include <chrono>
#include <ctime>
#include <memory>

namespace foundation
{

struct HighResolutionWaitImpl
{
    bool HighResolutionActive{ true };
};

HighResolutionWait::HighResolutionWait() :
    impl{ std::make_unique<HighResolutionWaitImpl>() }
{
}

HighResolutionWait::~HighResolutionWait() = default;

HighResolutionWait::HighResolutionWait(HighResolutionWait&& other) noexcept = default;
HighResolutionWait& HighResolutionWait::operator=(HighResolutionWait&& other) noexcept = default;

bool HighResolutionWait::HasHighResolutionSupport() const noexcept
{
    return true;
}

void HighResolutionWait::SleepUntil(const std::chrono::steady_clock::time_point& deadline) noexcept
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();

    if (deadline <= now)
    {
        return;
    }

    SleepFor(deadline - now);
}

void HighResolutionWait::SleepFor(const std::chrono::steady_clock::duration& duration) noexcept
{
    if (duration <= std::chrono::steady_clock::duration::zero())
    {
        return;
    }

    const int64_t totalNsec = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
    struct timespec req{};

    req.tv_sec = static_cast<time_t>(totalNsec / 1'000'000'000LL);
    req.tv_nsec = static_cast<long>(totalNsec % 1'000'000'000LL);

    while (nanosleep(&req, &req) == -1 && errno == EINTR)
    {
    }
}

} // namespace foundation
