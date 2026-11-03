#include "threading/PrecisionTimer.hpp"
#include <chrono>
#include <memory>
#include <time.h>
#include <unistd.h>
#include <cerrno>
#include <algorithm>
#include <thread>

#if __has_include(<sys/prctl.h>)
#include <sys/prctl.h>
#endif

namespace foundation
{

struct PlatformTimerImpl
{
    bool HighResolutionActive{ true };
};

PrecisionTimer::PrecisionTimer()
    : impl(std::make_unique<PlatformTimerImpl>())
{
#if defined(PR_SET_TIMERSLACK)
    prctl(PR_SET_TIMERSLACK, 1);
#endif
}

PrecisionTimer::~PrecisionTimer() = default;

PrecisionTimer::PrecisionTimer(PrecisionTimer&& other) noexcept = default;
PrecisionTimer& PrecisionTimer::operator=(PrecisionTimer&& other) noexcept = default;

bool PrecisionTimer::HasHighResolutionSupport() const noexcept
{
    return true;
}

void PrecisionTimer::SleepUntil(const std::chrono::steady_clock::time_point& deadline) noexcept
{
    const auto now = std::chrono::steady_clock::now();
    if (deadline <= now)
    {
        return;
    }

    const auto totalNsec = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline.time_since_epoch()).count();
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(totalNsec / 1'000'000'000LL);
    ts.tv_nsec = static_cast<long>(totalNsec % 1'000'000'000LL);

    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR)
    {
    }
}

void PrecisionTimer::SleepFor(const std::chrono::steady_clock::duration& duration) noexcept
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
