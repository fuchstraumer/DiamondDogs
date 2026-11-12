#include "timing/HighResolutionWait.hpp"
#include <cerrno>
#include <chrono>
#include <memory>
#include <time.h>
#include <unistd.h>

#if __has_include(<sys/prctl.h>)
#include <sys/prctl.h>
#endif

namespace foundation
{

struct HighResolutionWaitImpl
{
    bool HighResolutionActive{ true };
};

HighResolutionWait::HighResolutionWait() : impl(std::make_unique<HighResolutionWaitImpl>())
{
#if defined(PR_SET_TIMERSLACK)
    prctl(PR_SET_TIMERSLACK, 1);
#endif
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

    // libstdc++ and libc++ build steady_clock on CLOCK_MONOTONIC, so the deadline is an absolute CLOCK_MONOTONIC time
    const auto totalNsec = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count();
    struct timespec ts;

    ts.tv_sec = static_cast<time_t>(totalNsec / 1'000'000'000LL);
    ts.tv_nsec = static_cast<long>(totalNsec % 1'000'000'000LL);

    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR)
    {
    }
}

void HighResolutionWait::SleepFor(const std::chrono::steady_clock::duration& duration) noexcept
{
    if (duration <= std::chrono::steady_clock::duration::zero())
    {
        return;
    }

    const auto totalNsec = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    struct timespec req;

    req.tv_sec = static_cast<time_t>(totalNsec / 1'000'000'000LL);
    req.tv_nsec = static_cast<long>(totalNsec % 1'000'000'000LL);

    while (nanosleep(&req, &req) == -1 && errno == EINTR)
    {
    }
}

} // namespace foundation
