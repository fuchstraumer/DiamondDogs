#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_WINDOW_HPP
#define DIAMOND_DOGS_PLATFORM_WINDOW_HPP
#include "utility/delegate.hpp"
#include <memory>
#include <functional>

enum class windowing_mode : unsigned int
{
    None = 0,
    Fullscreen = 1,
    BorderlessWindowed = 2,
    Windowed = 3
};

struct WindowCallbackLists;


class PlatformWindow
{
    PlatformWindow(const PlatformWindow&) = delete;
    PlatformWindow& operator=(const PlatformWindow&) = delete;
public:

    PlatformWindow(int width, int height, const char* application_name, windowing_mode mode);
    ~PlatformWindow();

    void SetWindowUserPointer(void* user_ptr);
    void GetWindowSize(int& w, int& h) noexcept;
    void Update();
    void WaitForEvents();
    bool WindowShouldClose();

    using CursorPosCallbackType = delegate_t<void(double pos_x, double pos_y)>;
    using CursorEnterCallbackType = delegate_t<void(int enter)>;
    using ScrollCallbackType = delegate_t<void(double scroll_x, double scroll_y)>;
    using CharCallbackType = delegate_t<void(unsigned int code_point)>;
    using PathDropCallbackType = delegate_t<void(int count, const char** paths)>;
    using MouseButtonCallbackType = delegate_t<void(int button, int action, int mods)>;
    using KeyboardKeyCallbackType = delegate_t<void(int key, int scancode, int action, int mods)>;

    void AddCursorPosCallbackFn(CursorPosCallbackType fn);
    void AddCursorEnterCallbackFn(CursorEnterCallbackType fn);
    void AddScrollCallbackFn(ScrollCallbackType fn);
    void AddCharCallbackFn(CharCallbackType fn);
    void AddPathDropCallbackFn(PathDropCallbackType fn);
    void AddMouseButtonCallbackFn(MouseButtonCallbackType fn);
    void AddKeyboardKeyCallbackFn(KeyboardKeyCallbackType fn);
    void SetInputMode(int mode, int value);

    WindowCallbackLists& GetCallbacks() noexcept;

private:

    friend class RhiSystem;
    struct GLFWwindow* glfwWindow() noexcept;

    void createWindow(const char* app_name);
    void setCallbacks();

    struct GLFWwindow* window;
    std::unique_ptr<WindowCallbackLists> callbacks;
    windowing_mode windowMode;
    int width;
    int height;

};

#endif //!DIAMOND_DOGS_PLATFORM_WINDOW_HPP
