#pragma once
#ifndef VPSK_DEPTH_TARGET_HPP
#define VPSK_DEPTH_TARGET_HPP
#include "ForwardDecl.hpp"
#include <memory>

namespace vpsk {

    class DepthTarget {
        DepthTarget(const DepthTarget& other) = delete;
        DepthTarget& operator=(const DepthTarget& other) = delete;
    public:
        DepthTarget();
        ~DepthTarget();

        void Create(uint32_t width, uint32_t height, uint32_t sample_count_flags);
        void CreateAsCube(uint32_t size, bool independent_faces = false);

        const vpr::Image* GetImage() const noexcept;
        const vpr::Image* GetImageMSAA() const noexcept;
    
    private:
        std::unique_ptr<vpr::Image> image;
        std::unique_ptr<vpr::Image> imageMSAA;
        mutable bool msaaUpToDate{ false };
    };

}

#endif //!VPSK_DEPTH_TARGET_HPP
