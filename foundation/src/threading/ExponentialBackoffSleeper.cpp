#include "threading/ExponentialBackoffSleeper.hpp"

namespace foundation
{

    ExponentialBackoffSleeper::ExponentialBackoffSleeper(
        std::chrono::milliseconds minDuration,
        std::chrono::milliseconds maxDuration,
        float jitter,
        float multiplier) noexcept : 
        minSleepDuration(minDuration),
        maxSleepDuration(maxDuration),
        jitterFactor(jitter),
        backoffMultiplier(multiplier),
        currentSleepDuration(minDuration),
        randomGenerator(std::random_device{}())
    {
    }

    void ExponentialBackoffSleeper::reset() noexcept
    {
        currentSleepDuration = minSleepDuration;
    }

    void ExponentialBackoffSleeper::backoff() noexcept
    {
        // Increase sleep duration exponentially up to the maximum
        std::chrono::milliseconds newDuration = std::min
        (
            std::chrono::milliseconds(static_cast<long long>(currentSleepDuration.count() * backoffMultiplier)),
            maxSleepDuration
        );
        
        currentSleepDuration = newDuration;
        
        // Apply jitter if configured
        if (jitterFactor > 0.0f)
        {
            applyJitter();
        }
    }

    void ExponentialBackoffSleeper::sleepAndBackoff()
    {
        sleep();
        backoff();
    }

    void ExponentialBackoffSleeper::sleep() const
    {
        std::this_thread::sleep_for(currentSleepDuration);
    }

    std::chrono::milliseconds ExponentialBackoffSleeper::getCurrentDuration() const noexcept
    {
        return currentSleepDuration;
    }

    void ExponentialBackoffSleeper::applyJitter() noexcept
    {
        // Calculate jitter range
        float minFactor = 1.0f - jitterFactor;
        float maxFactor = 1.0f + jitterFactor;
        
        // Generate random jitter factor
        std::uniform_real_distribution<float> distribution(minFactor, maxFactor);
        float factor = distribution(randomGenerator);
        
        // Apply jitter
        auto jitteredDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::duration<float, std::milli>(currentSleepDuration.count() * factor)
        );
        
        // Ensure we stay within bounds
        currentSleepDuration = std::min
        (
            std::max(jitteredDuration, minSleepDuration),
            maxSleepDuration
        );
    }

} // namespace foundation
