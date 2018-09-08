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
    callbacks = std::make_unique<WindowCallbackLists>();
    setCallbacks();
}

PlatformWindow::~PlatformWindow() {
    glfwDestroyWindow(window);
    glfwTerminate();
}

void PlatformWindow::SetWindowUserPointer(void* user_ptr) {
    glfwSetWindowUserPointer(window, user_ptr);
}

GLFWwindow* PlatformWindow::glfwWindow() noexcept {
    return window;
}
void PlatformWindow::GetWindowSize(int& w, int& h) noexcept {
    glfwGetWindowSize(window, &width, &height);
    w = width;
    h = height;
}

void PlatformWindow::Update() {
    glfwPollEvents();
}

void PlatformWindow::WaitForEvents() {
    glfwWaitEvents();
}

bool PlatformWindow::WindowShouldClose() {
    return glfwWindowShouldClose(window);
}

void PlatformWindow::AddCursorPosCallbackFn(cursor_pos_callback_t fn) {
    callbacks->cursorPosCallbacks.push_front(fn);
}

void PlatformWindow::AddCursorEnterCallbackFn(cursor_enter_callback_t fn) {
    callbacks->cursorEnterCallbacks.push_front(fn);
}

void PlatformWindow::AddScrollCallbackFn(scroll_callback_t fn) {
    callbacks->scrollCallbacks.push_front(fn);
}

void PlatformWindow::AddCharCallbackFn(char_callback_t fn) {
    callbacks->charCallbacks.push_front(fn);
}

void PlatformWindow::AddPathDropCallbackFn(path_drop_callback_t fn) {
    callbacks->pathDropCallbacks.push_front(fn);
}

void PlatformWindow::AddMouseButtonCallbackFn(mouse_button_callback_t fn) {
    callbacks->mouseButtonCallbacks.push_front(fn);
}

void PlatformWindow::AddKeyboardKeyCallbackFn(keyboard_key_callback_t fn) {
    callbacks->keyboardKeyCallbacks.push_front(fn);
}

void PlatformWindow::SetInputMode(int mode, int value) {
    glfwSetInputMode(window, mode, value);
}

WindowCallbackLists& PlatformWindow::GetCallbacks() {
    return *callbacks;
}

void PlatformWindow::createWindow(const char* name) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (windowMode == windowing_modes::Fullscreen) {
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode* vidmode = glfwGetVideoMode(monitor);
        glfwWindowHint(GLFW_RED_BITS, vidmode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, vidmode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, vidmode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, vidmode->refreshRate);
        width = vidmode->width;
        height = vidmode->height;
        window = glfwCreateWindow(width, height, name, monitor, nullptr);
    }
    else if (windowMode == windowing_modes::BorderlessWindowed) {
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        window = glfwCreateWindow(width, height, name, nullptr, nullptr);
    }
    else {
        window = glfwCreateWindow(width, height, name, nullptr, nullptr);
    }
    glfwSetWindowUserPointer(window, this);
}

void PlatformWindow::setCallbacks() {
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetCursorEnterCallback(window, CursorEnterCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetCharCallback(window, CharCallback);
    glfwSetDropCallback(window, PathDropCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetKeyCallback(window, KeyboardKeyCallback);
}
