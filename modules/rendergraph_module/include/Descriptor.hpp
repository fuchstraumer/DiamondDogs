#pragma once
#ifndef DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#define DIAMOND_DOGS_DESCRIPTOR_SET_HPP
#include "ForwardDecl.hpp"
#include <memory>
#include <vulkan/vulkan.h>
struct VulkanResource;

class Descriptor {
public:

    void BindResourceToIdx(size_t idx, VulkanResource * rsrc);
private:
};

#endif // !DIAMOND_DOGS_DESCRIPTOR_SET_HPP
