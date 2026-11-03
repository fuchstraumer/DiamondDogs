#pragma once
#ifndef PLATFORM_SYSTEM_IMPL_HPP
#define PLATFORM_SYSTEM_IMPL_HPP
#include "PlatformTypes.hpp"
#include "PlatformSystem.hpp" // unfortunate, but has delegate type aliases
#include "events/PlatformEvents.hpp"
#include "GlfwInputSource.hpp"
#include <vector>
#include <memory>

/**
 * @file PlatformSystemImpl.hpp
 * @brief Implementation details for the PlatformSystem module, mostly private functions that would otherwise clutter the source code
 * of the platform system
*/


struct GLFWwindow;
struct CallbackStorage;
class Swapchain;
class PlatformSurface;

struct PlatformSystemImpl
{
    PlatformSystemImpl(const PlatformWindowCreateInfo& createInfo);
    ~PlatformSystemImpl();
    struct GLFWwindow* Window = nullptr;
    DisplayInfo ActiveDisplay;
    std::vector<DisplayInfo> AllDisplays;
    std::unique_ptr<CallbackStorage> Callbacks;
    std::unique_ptr<PlatformSurface> ActiveSurface;
    std::unique_ptr<Swapchain> ActiveSwapchain;
    GlfwInputSource InputSource;
    // The close listeners fire once per close request, not once per Update while the request stands
    bool CloseReported = false;
    // Deletes GLFWwindow, but does not terminate GLFW (done in dtor)
    void Destroy();
    void Update();
    void WaitForEvents();
    void SetInputAssembler(input::FrameAssembler* assembler);
    void AddShouldResizeEventListener(const ShouldResizeEvent& listener, void* user_data);
    void AddShouldCloseEventListener(const ShouldCloseEvent& listener, void* user_data);
};

void ResizeCallback(GLFWwindow* window, int width, int height);
void ShouldCloseCallback(GLFWwindow* window);

#endif //!PLATFORM_SYSTEM_IMPL_HPP
