#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_POOL_POOL_HPP
#define DIAMOND_DOGS_DESCRIPTOR_POOL_POOL_HPP
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include "ForwardDecl.hpp"

namespace st {
    struct descriptor_type_counts_t;
}

class DescriptorPool {
public:

    DescriptorPool(const st::descriptor_type_counts_t& counts);

private: 
    std::unique_ptr<vpr::DescriptorPool> pool;
};

#endif // !DIAMOND_DOGS_DESCRIPTOR_POOL_POOL_HPP
