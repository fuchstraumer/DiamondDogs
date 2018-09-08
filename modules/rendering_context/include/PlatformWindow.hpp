#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_WINDOW_HPP
#define DIAMOND_DOGS_PLATFORM_WINDOW_HPP
#include "core/signal/delegate.hpp"
#include <memory>

enum class windowing_mode : unsigned int {
    None = 0,
    Fullscreen = 1,
    BorderlessWindowed = 2,
    Windowed = 3
};

struct WindowCallbackLists;

using cursor_pos_callback_t = delegate_t<void(double pos_x, double pos_y)>;
using cursor_enter_callback_t = delegate_t<void(int enter)>;
using scroll_callback_t = delegate_t<void(double scroll_x, double scroll_y)>;
using char_callback_t = delegate_t<void(unsigned int code_point)>;
using path_drop_callback_t = delegate_t<void(int count, const char** paths)>;
using mouse_button_callback_t = delegate_t<void(int button, int action, int mods)>;
using keyboard_key_callback_t = delegate_t<void(int key, int scan_code, int action, int mods)>;

class PlatformWindow {
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

    void AddCursorPosCallbackFn(cursor_pos_callback_t fn);
    void AddCursorEnterCallbackFn(cursor_enter_callback_t fn);
    void AddScrollCallbackFn(scroll_callback_t fn);
    void AddCharCallbackFn(char_callback_t fn);
    void AddPathDropCallbackFn(path_drop_callback_t fn);
    void AddMouseButtonCallbackFn(mouse_button_callback_t fn);
    void AddKeyboardKeyCallbackFn(keyboard_key_callback_t fn);
    void SetInputMode(int mode, int value);

    WindowCallbackLists& GetCallbacks() noexcept;

private:

    friend class RenderingContext;
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
