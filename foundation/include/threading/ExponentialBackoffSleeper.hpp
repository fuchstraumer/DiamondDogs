#pragma once
#ifndef FOUNDATION_THREADING_EXPONENTIAL_BACKOFF_SLEEPER_HPP
#define FOUNDATION_THREADING_EXPONENTIAL_BACKOFF_SLEEPER_HPP

#include <chrono>
#include <random>
#include <thread>

namespace foundation
{
    /**
     * A utility class that implements jittered exponential backoff for sleeping threads.
     * Can be reused across different worker threads that need similar backoff behavior.
     */
    struct ExponentialBackoffSleeper
    {
        /**
         * Constructs a new ExponentialBackoffSleeper with the specified parameters.
         * 
         * @param minDuration Minimum sleep duration in milliseconds
         * @param maxDuration Maximum sleep duration in milliseconds
         * @param jitter Amount of jitter to apply (0.0 = none, 0.3 = ±30%)
         * @param multiplier Factor to multiply exponential sleep duration by when backing off. 1.0 means backoff is just pow2(backoffCount)
         */
        ExponentialBackoffSleeper(
            std::chrono::milliseconds minDuration = std::chrono::milliseconds(1),
            std::chrono::milliseconds maxDuration = std::chrono::milliseconds(500),
            float jitter = 0.3f,
            float multiplier = 1.0f
        ) noexcept;

        ~ExponentialBackoffSleeper() noexcept = default;

        ExponentialBackoffSleeper(const ExponentialBackoffSleeper&) = delete;
        ExponentialBackoffSleeper& operator=(const ExponentialBackoffSleeper&) = delete;

        ExponentialBackoffSleeper(ExponentialBackoffSleeper&&) noexcept = default;
        ExponentialBackoffSleeper& operator=(ExponentialBackoffSleeper&&) noexcept = default;
        
        /**
         * Resets the sleep duration to the minimum value.
         * Call this when work has been done and you want to be responsive.
         */
        void reset() noexcept;
        
        /**
         * Increases the sleep duration according to the backoff strategy.
         * Call this when no work was done and you want to back off.
         */
        void backoff() noexcept;
        
        /**
         * Sleeps for the current duration and then backs off.
         * Convenience method for when no work was done.
         */
        void sleepAndBackoff();
        
        /**
         * Sleeps for the current duration.
         */
        void sleep() const;
        
        /**
         * Returns the current sleep duration.
         */
        std::chrono::milliseconds getCurrentDuration() const noexcept;
        
    private:
        /**
         * Applies jitter to the current sleep duration.
         */
        void applyJitter() noexcept;       
        
        std::chrono::milliseconds minSleepDuration;
        std::chrono::milliseconds maxSleepDuration;
        float jitterFactor; // 0.0 = no jitter, 0.3 = ±30% jitter
        float backoffMultiplier;
        std::chrono::milliseconds currentSleepDuration;
        std::mt19937 randomGenerator;
        size_t backoffCount{ 1u };
    };
    
} // namespace foundation

#endif // FOUNDATION_THREADING_EXPONENTIAL_BACKOFF_SLEEPER_HPP
