#pragma once
#ifndef PLATFORM_SYSTEM_IMPL_HPP
#define PLATFORM_SYSTEM_IMPL_HPP
#include "PlatformTypes.hpp"
#include "PlatformSystem.hpp" // unfortunate, but has delegate type aliases
#include "events/PlatformEvents.hpp"
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
    // Deletes GLFWwindow, but does not terminate GLFW (done in dtor)
    void Destroy();
    void Update();
    void WaitForEvents();
    void AddCursorPosEventListener(const CursorPosEvent& listener, void* user_data);
    void AddCursorEnterEventListener(const CursorEnterEvent& listener, void* user_data);
    void AddScrollEventListener(const ScrollEvent& listener, void* user_data);
    void AddCharEventListener(const CharEvent& listener, void* user_data);
    void AddPathDropEventListener(const PathDropEvent& listener, void* user_data);
    void AddMouseButtonEventListener(const MouseButtonEvent& listener, void* user_data);
    void AddKeyboardKeyEventListener(const KeyboardKeyEvent& listener, void* user_data);
    void AddShouldResizeEventListener(const ShouldResizeEvent& listener, void* user_data);
    void AddShouldCloseEventListener(const ShouldCloseEvent& listener, void* user_data);
};

void CursorPosCallback(GLFWwindow* window, double pos_x, double pos_y);
void CursorEnterCallback(GLFWwindow* window, int enter);
void ScrollCallback(GLFWwindow* window, double x_offset, double y_offset);
void CharCallback(GLFWwindow* window, unsigned int code);
void PathDropCallback(GLFWwindow* window, int count, const char** paths);
void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
void KeyboardKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void ResizeCallback(GLFWwindow* window, int width, int height);
void ShouldCloseCallback(GLFWwindow* window);

#endif //!PLATFORM_SYSTEM_IMPL_HPP
