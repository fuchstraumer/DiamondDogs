#pragma once
#ifndef TASK_GRAPH_UNIFIED_TIMELINE_SEMAPHORE_HPP
#define TASK_GRAPH_UNIFIED_TIMELINE_SEMAPHORE_HPP
#include <atomic>
#include <semaphore>
#include <chrono>
#include <vulkan/vulkan_core.h>

namespace TaskGraph
{

/**
 * @brief Unified timeline semaphore that can work with both CPU and GPU operations
 * 
 * This class provides timeline semaphore semantics using C++20 counting_semaphore
 * for CPU operations, while also supporting Vulkan timeline semaphores for GPU work.
 */
class UnifiedTimelineSemaphore
{
public:
    enum class SemaphoreType : uint8_t
    {
        Invalid = 0,
        CPU,        // Uses std::counting_semaphore, to synchronize CPU only tasks
        GPU,        // Uses a Vulkan timeline semaphore to synchronize GPU only tasks
        GPUToCPU,   // Uses a Vulkan timeline semaphore to synchronize GPU to CPU tasks
        CPUToGPU    // Uses a Vulkan timeline semaphore to synchronize CPU to GPU tasks
    };

private:
    SemaphoreType type{SemaphoreType::Invalid };
    std::atomic<uint64_t> currentValue{0};
    std::counting_semaphore<> cpuSemaphore{0};
    
    // GPU semaphore data (only valid for GPU or Hybrid types)
    VkSemaphore vulkanSemaphore{ VK_NULL_HANDLE };
    VkDevice vulkanDevice{ VK_NULL_HANDLE };

public:
    explicit UnifiedTimelineSemaphore(SemaphoreType _type) : type(_type) {}
    
    // Constructor for GPU-enabled semaphores
    UnifiedTimelineSemaphore(
        SemaphoreType _type,
        VkDevice device,
        VkSemaphore semaphore)
        : type(_type), vulkanDevice(device), vulkanSemaphore(semaphore) {}

    /**
     * @brief Signal the semaphore to the specified value
     * @param targetValue The value to signal to
     */
    void signal(uint64_t targetValue) noexcept
    {
        switch (type)
        {
            case SemaphoreType::CPU:
                signalCPU(targetValue);
                break;
            case SemaphoreType::GPU:
                signalGPU(targetValue);
                break;
            case SemaphoreType::GPUToCPU:
                signalGPUToCPU(targetValue);
                break;
            case SemaphoreType::CPUToGPU:
                signalCPUToGPU(targetValue);
                break;
        }
    }

    /**
     * @brief Wait for the semaphore to reach the specified value
     * @param targetValue The value to wait for
     * @param timeout_ns Timeout in nanoseconds (0 = no timeout)
     * @return true if wait succeeded, false if timeout
     */
    bool wait(uint64_t targetValue, uint64_t timeout_ns = 0) noexcept
    {
        switch (type)
        {
            case SemaphoreType::CPU:
                return waitCPU(targetValue, timeout_ns);
            case SemaphoreType::GPUToCPU:
                return waitGPUToCPU(targetValue, timeout_ns);
            case SemaphoreType::GPU:
            case SemaphoreType::CPUToGPU:
            default:
                return false; // Unsupported wait types
        }
    }

    /**
     * @brief Get current semaphore value
     */
    uint64_t getValue() const noexcept
    {
        if (type == SemaphoreType::GPU_Vulkan && vulkanSemaphore != VK_NULL_HANDLE)
        {
            uint64_t value;
            VkResult result = vkGetSemaphoreCounterValue(vulkanDevice, vulkanSemaphore, &value);
            return (result == VK_SUCCESS) ? value : currentValue.load();
        }
        
        return currentValue.load();
    }

    /**
     * @brief Check if semaphore has reached target value without blocking
     */
    bool hasReached(uint64_t targetValue) const noexcept
    {
        return getValue() >= targetValue;
    }

    VkSemaphore getVulkanHandle() const noexcept
    { 
        return vulkanSemaphore; 
    }

private:

    void signalCPU(uint64_t targetValue) noexcept
    {
        uint64_t oldValue = currentValue.exchange(targetValue);
        
        if (targetValue > oldValue)
        {
            uint64_t increment = targetValue - oldValue;
            cpuSemaphore.release(static_cast<std::ptrdiff_t>(increment));
        }
    }

    void signalGPU(uint64_t targetValue) noexcept;
    void signalGPUToCPU(uint64_t targetValue) noexcept;
    void signalCPUToGPU(uint64_t targetValue) noexcept;

    bool waitCPU(uint64_t targetValue, uint64_t timeout_ns) noexcept
    {
        if (currentValue.load() >= targetValue)
        {
            return true;
        }

        auto start = std::chrono::steady_clock::now();
        
        while (currentValue.load() < targetValue)
        {
            if (timeout_ns > 0)
            {
                auto elapsed = std::chrono::steady_clock::now() - start;
                auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
                
                if (static_cast<uint64_t>(elapsed_ns) >= timeout_ns)
                {
                    return false;
                }
                
                auto remaining_ns = timeout_ns - elapsed_ns;
                auto timeout_duration = std::chrono::nanoseconds(std::min(remaining_ns, 1000000ULL)); // Max 1ms wait
                
                if (!cpuSemaphore.try_acquire_for(timeout_duration))
                {
                    continue; // Try again with remaining time
                }
            }
            else
            {
                cpuSemaphore.acquire();
            }
        }
        
        return true;
    }

    /** @brief Wait on the CPU for GPU work to complete - thus, wait for given semaphore to reach target value */
    bool waitGPUToCPU(uint64_t targetValue, uint64_t timeout_ns) noexcept
    {
        if (vulkanSemaphore == VK_NULL_HANDLE || vulkanDevice == VK_NULL_HANDLE)
        {
            return false;
        }

        VkSemaphoreWaitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &vulkanSemaphore;
        waitInfo.pValues = &targetValue;

        uint64_t timeout = (timeout_ns == 0) ? UINT64_MAX : timeout_ns;
        VkResult result = vkWaitSemaphores(vulkanDevice, &waitInfo, timeout);
        
        if (result == VK_SUCCESS)
        {
            currentValue.store(std::max(currentValue.load(), targetValue));
            return true;
        }
        
        return false;
    }
};

} // namespace TaskGraph

#endif // TASK_GRAPH_UNIFIED_TIMELINE_SEMAPHORE_HPP
