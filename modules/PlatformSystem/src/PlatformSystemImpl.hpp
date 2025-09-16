#pragma once
#ifndef PLATFORM_SYSTEM_IMPL_HPP
#define PLATFORM_SYSTEM_IMPL_HPP
#include "PlatformTypes.hpp"
#include "events/PlatformEvents.hpp"
#include <vector>
#include <memory>

/**
 * @file PlatformSystemImpl.hpp
 * @brief Implementation details for the PlatformSystem module, mostly private functions that would otherwise clutter the source code
 * of the platform system
*/

// We use these as storage buffers before init 
DisplayInfo s_PrimaryDisplay;

struct GLFWwindow;
struct CallbackStorage;


struct PlatformSystemImpl
{
    PlatformSystemImpl(const PlatformWindowCreateInfo& createInfo);
    ~PlatformSystemImpl();
    struct GLFWwindow* Window = nullptr;
    // pointer to ALlDisplays, describes active display
    DisplayInfo* ActiveDisplay = nullptr;
    std::vector<DisplayInfo> AllDisplays;
    std::unique_ptr<CallbackStorage> Callbacks;
    void Update();
    void WaitForEvents();
    void AddCursorPosEventListener(PlatformWindowSystem::CursorPosEvent listener, void* userData);
    void AddCursorEnterEventListener(PlatformWindowSystem::CursorEnterEvent listener, void* userData);
    void AddScrollEventListener(PlatformWindowSystem::ScrollEvent listener, void* userData);
    void AddCharEventListener(PlatformWindowSystem::CharEvent listener, void* userData);
    void AddPathDropEventListener(PlatformWindowSystem::PathDropEvent listener, void* userData);
    void AddMouseButtonEventListener(PlatformWindowSystem::MouseButtonEvent listener, void* userData);
    void AddKeyboardKeyEventListener(PlatformWindowSystem::KeyboardKeyEvent listener, void* userData);
    void AddShouldResizeEventListener(PlatformWindowSystem::ShouldResizeEvent listener, void* userData);
    void AddShouldCloseEventListener(PlatformWindowSystem::ShouldCloseEvent listener, void* userData);
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
