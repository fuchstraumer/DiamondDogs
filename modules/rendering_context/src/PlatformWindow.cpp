#include "PlatformWindow.hpp"
#include "RenderingContext.hpp"
#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#if defined(_WIN32) 
#undef APIENTRY
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#include "GLFW/glfw3native.h"
#endif
#include <forward_list>

struct WindowCallbackLists {
    std::forward_list<cursor_pos_callback_t> cursorPosCallbacks;
    std::forward_list<cursor_enter_callback_t> cursorEnterCallbacks;
    std::forward_list<scroll_callback_t> scrollCallbacks;
    std::forward_list<char_callback_t> charCallbacks;
    std::forward_list<path_drop_callback_t> pathDropCallbacks;
    std::forward_list<mouse_button_callback_t> mouseButtonCallbacks;
    std::forward_list<keyboard_key_callback_t> keyboardKeyCallbacks;
};


static void CursorPosCallback(GLFWwindow* window, double pos_x, double pos_y) {
    PlatformWindow* user_ptr = reinterpret_cast<PlatformWindow*>(glfwGetWindowUserPointer(window));
    auto& callbacks = user_ptr->GetCallbacks();
    for (auto& pos_fn : callbacks.cursorPosCallbacks) {
        pos_fn(pos_x, pos_y);
    }
}

static void CursorEnterCallback(GLFWwindow* window, int enter) {
    PlatformWindow* user_ptr = reinterpret_cast<PlatformWindow*>(glfwGetWindowUserPointer(window));
    auto& callbacks = user_ptr->GetCallbacks();
    for (auto& enter_fn : callbacks.cursorEnterCallbacks) {
        enter_fn(enter);
    }
}

static void ScrollCallback(GLFWwindow* window, double x_offset, double y_offset) {
    PlatformWindow* user_ptr = reinterpret_cast<PlatformWindow*>(glfwGetWindowUserPointer(window));
    auto& callbacks = user_ptr->GetCallbacks();
    for (auto& scroll_fn : callbacks.scrollCallbacks) {
        scroll_fn(x_offset, y_offset);
    }
}

static void CharCallback(GLFWwindow* window, unsigned int code) {
    PlatformWindow* user_ptr = reinterpret_cast<PlatformWindow*>(glfwGetWindowUserPointer(window));
    auto& callbacks = user_ptr->GetCallbacks();
    for (auto& char_fn : callbacks.charCallbacks) {
        char_fn(code);
    }
}

static void PathDropCallback(GLFWwindow* window, int count, const char** paths) {
    PlatformWindow* user_ptr = reinterpret_cast<PlatformWindow*>(glfwGetWindowUserPointer(window));
    auto& callbacks = user_ptr->GetCallbacks();
    for (auto& drop_fn : callbacks.pathDropCallbacks) {
        drop_fn(count, paths);
    }
}

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    PlatformWindow* user_ptr = reinterpret_cast<PlatformWindow*>(glfwGetWindowUserPointer(window));
    auto& callbacks = user_ptr->GetCallbacks();
    for (auto& mouse_fn : callbacks.mouseButtonCallbacks) {
        mouse_fn(button, action, mods);
    }
}

static void KeyboardKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    PlatformWindow* user_ptr = reinterpret_cast<PlatformWindow*>(glfwGetWindowUserPointer(window));
    auto& callbacks = user_ptr->GetCallbacks();
    for (auto& key_fn : callbacks.keyboardKeyCallbacks) {
        key_fn(key, scancode, action, mods);
    }
}

static void ResizeCallback(GLFWwindow* window, int width, int height) {
    RenderingContext::SetShouldResize(true);
}

PlatformWindow::PlatformWindow(int width, int height, const char* application_name, windowing_mode mode) : width(width), height(height), windowMode(mode) {
    createWindow(application_name);
    glfwSetWindowSizeCallback(window, ResizeCallback);
}

