#pragma once
#ifndef VPSK_DEPTH_TARGET_HPP
#define VPSK_DEPTH_TARGET_HPP
#include "ForwardDecl.hpp"
#include <cstdint>

class DepthTarget {
    DepthTarget(const DepthTarget& other) = delete;
    DepthTarget& operator=(const DepthTarget& other) = delete;
public:
    DepthTarget();
    ~DepthTarget();

    void Create(uint32_t width, uint32_t height, uint32_t sample_count_flags);
    void CreateAsCube(uint32_t size, bool independent_faces = false);

    const VulkanResource* GetImage() const noexcept;
    const VulkanResource* GetImageMSAA() const noexcept;
    
private:
    VulkanResource* image;
    VulkanResource* imageMSAA;
    mutable bool msaaUpToDate{ false };
};

#endif //!VPSK_DEPTH_TARGET_HPP
