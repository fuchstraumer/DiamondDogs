#include "PlatformSystem.hpp"
#include "PlatformSystemImpl.hpp"
#include "ImageDataFormats.hpp"
#include "Swapchain.hpp"
#include "PlatformSurface.hpp"
#include <vector>
#include <unordered_map>
#include <stdexcept>

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"
#if defined(_WIN32)
#undef APIENTRY
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"
#endif


static DisplayInfo s_PrimaryDisplay;

struct CallbackStorage
{
    // we associate user data pointers with the hash of the delegate function
    using UserDataStorageType = std::unordered_map<size_t, void*>;

    std::vector<CursorPosEvent> CursorPosEvents;
    UserDataStorageType CursorPosEventUserData;
    std::vector<CursorEnterEvent> CursorEnterEvents;
    UserDataStorageType CursorEnterEventUserData;
    std::vector<ScrollEvent> ScrollEvents;
    UserDataStorageType ScrollEventUserData;
    std::vector<CharEvent> CharEvents;
    UserDataStorageType CharEventUserData;
    std::vector<PathDropEvent> PathDropEvents;
    UserDataStorageType PathDropEventUserData;
    std::vector<MouseButtonEvent> MouseButtonEvents;
    UserDataStorageType MouseButtonEventUserData;
    std::vector<KeyboardKeyEvent> KeyboardKeyEvents;
    UserDataStorageType KeyboardKeyEventUserData;
    std::vector<ShouldResizeEvent> ShouldResizeEvents;
    UserDataStorageType ShouldResizeEventUserData;
    std::vector<ShouldCloseEvent> ShouldCloseEvents;
    UserDataStorageType ShouldCloseEventUserData;
};

void CursorPosCallback(GLFWwindow* window, double pos_x, double pos_y)
{
    PlatformSystemImpl* user_ptr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));

    const Events::CursorPosEventData eventData{ pos_x, pos_y, window };

    for (auto& pos_fn : user_ptr->Callbacks->CursorPosEvents)
    {
        if (user_ptr->Callbacks->CursorPosEventUserData.contains(pos_fn.hash()))
        {
            pos_fn(eventData, user_ptr->Callbacks->CursorPosEventUserData[pos_fn.hash()]);
        }
        else
        {
            pos_fn(eventData, nullptr);
        }
    }
}

void CursorEnterCallback(GLFWwindow* window, int enter)
{
    PlatformSystemImpl* user_ptr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    for (auto& enter_fn : user_ptr->Callbacks->CursorEnterEvents)
    {
        if (user_ptr->Callbacks->CursorEnterEventUserData.contains(enter_fn.hash()))
        {
            enter_fn(enter, user_ptr->Callbacks->CursorEnterEventUserData[enter_fn.hash()]);
        }
        else
        {
            enter_fn(enter, nullptr);
        }
    }
}

void ScrollCallback(GLFWwindow* window, double x_offset, double y_offset)
{
    PlatformSystemImpl* user_ptr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::ScrollEventData eventData{ x_offset, y_offset, window };
    for (auto& scroll_fn : user_ptr->Callbacks->ScrollEvents)
    {
        if (user_ptr->Callbacks->ScrollEventUserData.contains(scroll_fn.hash()))
        {
            scroll_fn(eventData, user_ptr->Callbacks->ScrollEventUserData[scroll_fn.hash()]);
        }
        else
        {
            scroll_fn(eventData, nullptr);
        }
    }
}

void CharCallback(GLFWwindow* window, unsigned int code)
{
    PlatformSystemImpl* user_ptr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    for (auto& char_fn : user_ptr->Callbacks->CharEvents)
    {
        if (user_ptr->Callbacks->CharEventUserData.contains(char_fn.hash()))
        {
            char_fn(code, user_ptr->Callbacks->CharEventUserData[char_fn.hash()]);
        }
        else
        {
            char_fn(code, nullptr);
        }
    }
}

void PathDropCallback(GLFWwindow* window, int count, const char** paths)
{
    PlatformSystemImpl* user_ptr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::PathDropEventData eventData{ count, paths, window };
    for (auto& drop_fn : user_ptr->Callbacks->PathDropEvents)
    {
        if (user_ptr->Callbacks->PathDropEventUserData.contains(drop_fn.hash()))
        {
            drop_fn(eventData, user_ptr->Callbacks->PathDropEventUserData[drop_fn.hash()]);
        }
        else
        {
            drop_fn(eventData, nullptr);
        }
    }
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    PlatformSystemImpl* user_ptr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::MouseButtonEventData eventData{ button, action, mods, window };
    for (auto& mouse_fn : user_ptr->Callbacks->MouseButtonEvents)
    {
        if (user_ptr->Callbacks->MouseButtonEventUserData.contains(mouse_fn.hash()))
        {
            mouse_fn(eventData, user_ptr->Callbacks->MouseButtonEventUserData[mouse_fn.hash()]);
        }
        else
        {
            mouse_fn(eventData, nullptr);
        }
    }
}

void KeyboardKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    // Handle Alt + F4 to close window (default behavior)
    if (key == GLFW_KEY_F4 && action == GLFW_PRESS && (mods & GLFW_MOD_ALT))
    {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    PlatformSystemImpl* user_ptr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::KeyboardKeyEventData eventData{ key, scancode, action, mods, window };
    for (auto& key_fn : user_ptr->Callbacks->KeyboardKeyEvents)
    {
        if (user_ptr->Callbacks->KeyboardKeyEventUserData.contains(key_fn.hash()))
        {
            key_fn(eventData, user_ptr->Callbacks->KeyboardKeyEventUserData[key_fn.hash()]);
        }
        else
        {
            key_fn(eventData, nullptr);
        }
    }
}

void ResizeCallback(GLFWwindow* window, int width, int height)
{
    PlatformSystemImpl* user_ptr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::ShouldResizeEventData eventData{ width, height };
    for (auto& resize_fn : user_ptr->Callbacks->ShouldResizeEvents)
    {
        if (user_ptr->Callbacks->ShouldResizeEventUserData.contains(resize_fn.hash()))
        {
            resize_fn(eventData, user_ptr->Callbacks->ShouldResizeEventUserData[resize_fn.hash()]);
        }
        else
        {
            resize_fn(eventData, nullptr);
        }
    }
}

void ShouldCloseCallback(GLFWwindow* window)
{
    PlatformSystemImpl* user_ptr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    for (auto& close_fn : user_ptr->Callbacks->ShouldCloseEvents)
    {
        if (user_ptr->Callbacks->ShouldCloseEventUserData.contains(close_fn.hash()))
        {
            close_fn(user_ptr->Callbacks->ShouldCloseEventUserData[close_fn.hash()]);
        }
        else
        {
            close_fn(nullptr);
        }
    }
}

PlatformSystemImpl::PlatformSystemImpl(const PlatformWindowCreateInfo& createInfo) :
    Window(nullptr),
    ActiveDisplay{},
    AllDisplays(),
    Callbacks(std::make_unique<CallbackStorage>())
{
    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // for now, we just try to find the display with highest bit depth and use that as the primary display
    s_PrimaryDisplay = PlatformWindowSystem::GetPrimaryDisplayInfo();
    if (createInfo.DisplayToUse == nullptr && 
        (createInfo.DesiredWindowMode == PlatformWindowMode::Fullscreen ||
         createInfo.DesiredWindowMode == PlatformWindowMode::FullScreenWindowed))
    {
        ActiveDisplay = s_PrimaryDisplay;
    }
    else if (createInfo.DesiredWindowMode == PlatformWindowMode::Windowed)
    {
        // Set active display to inherit most of the primary display's properties, but override width/height with requested window size
        ActiveDisplay = s_PrimaryDisplay;
        ActiveDisplay.Width = createInfo.InitialWidth;
        ActiveDisplay.Height = createInfo.InitialHeight;
    }
    else if (createInfo.DesiredWindowMode == PlatformWindowMode::MaximizedWindowed)
    {
        // Set active display to inherit most of the primary display's properties, but override width/height with full display size
        // We'll correct it after creation by querying actual window size from GLFW
        ActiveDisplay = s_PrimaryDisplay;
        ActiveDisplay.Width = s_PrimaryDisplay.Width;
        ActiveDisplay.Height = s_PrimaryDisplay.Height;
    }
    else if (createInfo.DisplayToUse != nullptr)
    {
        ActiveDisplay = *createInfo.DisplayToUse;
    }

    // Create window with desired settings
    if (createInfo.BehaviorFlags.Resizable && 
        createInfo.DesiredWindowMode == PlatformWindowMode::Windowed)
    {
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    }
    else
    {
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    }

    if (createInfo.BehaviorFlags.Moveable)
    {
    }
    else
    {
    }

    // decorate only if flag set 
    if (createInfo.BehaviorFlags.Decorated && 
        (createInfo.DesiredWindowMode == PlatformWindowMode::Windowed ||
         createInfo.DesiredWindowMode == PlatformWindowMode::MaximizedWindowed))
    {
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    }
    else
    {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }

    // need to also make sure we acquire focus on show if we're going fullscreen
    if (createInfo.BehaviorFlags.FocusOnShow || (createInfo.DesiredWindowMode == PlatformWindowMode::Fullscreen))
    {
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    }
    else
    {
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    if (createInfo.BehaviorFlags.CenterMouse)
    {
        glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_TRUE);
    }
    else
    {
        glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_FALSE);
    }

    glfwWindowHint(GLFW_POSITION_X, static_cast<int>(createInfo.InitialPosX));
    glfwWindowHint(GLFW_POSITION_Y, static_cast<int>(createInfo.InitialPosY));
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // required to create a Vulkan-capable window, otherwise we create an OpenGL context by default

    switch (createInfo.DesiredWindowMode)
    {
    case PlatformWindowMode::Windowed:
        Window = glfwCreateWindow(static_cast<int>(createInfo.InitialWidth), static_cast<int>(createInfo.InitialHeight), createInfo.WindowName, nullptr, nullptr);
        break;
    case PlatformWindowMode::Fullscreen:
        // we only set this particular hint for fullscreen exclusive mode
        glfwWindowHint(GLFW_REFRESH_RATE, static_cast<int>(s_PrimaryDisplay.RefreshRateCapabilities.RoundedRefreshRate));
        Window = glfwCreateWindow(static_cast<int>(createInfo.InitialWidth), static_cast<int>(createInfo.InitialHeight), createInfo.WindowName, glfwGetPrimaryMonitor(), nullptr);
        break;
    case PlatformWindowMode::FullScreenWindowed:
        // borderless fullscreen windowed mode, set window size to match display size but avoid passing a monitor to avoid going exclusive fullscreen
        Window = glfwCreateWindow(static_cast<int>(ActiveDisplay.Width), static_cast<int>(ActiveDisplay.Height), createInfo.WindowName, nullptr, nullptr);
        break;
    case PlatformWindowMode::MaximizedWindowed:
        throw std::domain_error("Oops, I was too lazy to implement this video mode!");
    default:
        throw std::runtime_error("Invalid window mode, can't create GLFW window.");
    };

    if (createInfo.DesiredWindowMode == PlatformWindowMode::MaximizedWindowed)
    {
        int width, height;
        glfwGetWindowSize(Window, &width, &height);
        ActiveDisplay.Width = static_cast<uint32_t>(width);
        ActiveDisplay.Height = static_cast<uint32_t>(height);
    }

    if (!Window)
    {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    // Set resize callback, right now no listeners are connected to it but we'll connect them from other systems later
    glfwSetWindowSizeCallback(Window, ResizeCallback);

    // Set this impl as the user pointer for callbacks
    glfwSetWindowUserPointer(Window, this);
}

PlatformSystemImpl::~PlatformSystemImpl()
{
    Destroy();
    glfwTerminate();
}

void PlatformSystemImpl::Destroy()
{
    if (Window)
    {
        glfwDestroyWindow(Window);
        Window = nullptr;
    }
}

void PlatformSystemImpl::Update()
{
    if (!Window)
    {
        return;
    }

    glfwPollEvents();

    if (glfwWindowShouldClose(Window))
    {
        ShouldCloseCallback(Window);
        Destroy();
    }
}

void PlatformSystemImpl::WaitForEvents()
{
    glfwWaitEvents();
}

void PlatformSystemImpl::AddCursorPosEventListener(CursorPosEvent listener, void* userData)
{
    Callbacks->CursorPosEvents.push_back(listener);
    if (userData != nullptr)
    {
        Callbacks->CursorPosEventUserData[listener.hash()] = userData;
    }
}

void PlatformSystemImpl::AddCursorEnterEventListener(CursorEnterEvent listener, void* userData)
{
    Callbacks->CursorEnterEvents.push_back(listener);
    if (userData != nullptr)
    {
        Callbacks->CursorEnterEventUserData[listener.hash()] = userData;
    }
}

void PlatformSystemImpl::AddScrollEventListener(ScrollEvent listener, void* userData)
{
    Callbacks->ScrollEvents.push_back(listener);
    if (userData != nullptr)
    {
        Callbacks->ScrollEventUserData[listener.hash()] = userData;
    }
}

void PlatformSystemImpl::AddCharEventListener(CharEvent listener, void* userData)
{
    Callbacks->CharEvents.push_back(listener);
    if (userData != nullptr)
    {
        Callbacks->CharEventUserData[listener.hash()] = userData;
    }
}

void PlatformSystemImpl::AddPathDropEventListener(PathDropEvent listener, void* userData)
{
    Callbacks->PathDropEvents.push_back(listener);
    if (userData != nullptr)
    {
        Callbacks->PathDropEventUserData[listener.hash()] = userData;
    }
}

void PlatformSystemImpl::AddMouseButtonEventListener(MouseButtonEvent listener, void* userData)
{
    Callbacks->MouseButtonEvents.push_back(listener);
    if (userData != nullptr)
    {
        Callbacks->MouseButtonEventUserData[listener.hash()] = userData;
    }
}

void PlatformSystemImpl::AddKeyboardKeyEventListener(KeyboardKeyEvent listener, void* userData)
{
    Callbacks->KeyboardKeyEvents.push_back(listener);
    if (userData != nullptr)
    {
        Callbacks->KeyboardKeyEventUserData[listener.hash()] = userData;
    }
}

void PlatformSystemImpl::AddShouldResizeEventListener(ShouldResizeEvent listener, void* userData)
{
    Callbacks->ShouldResizeEvents.push_back(listener);
    if (userData != nullptr)
    {
        Callbacks->ShouldResizeEventUserData[listener.hash()] = userData;
    }
}

void PlatformSystemImpl::AddShouldCloseEventListener(ShouldCloseEvent listener, void* userData)
{
    Callbacks->ShouldCloseEvents.push_back(listener);
    if (userData != nullptr)
    {
        Callbacks->ShouldCloseEventUserData[listener.hash()] = userData;
    }
}
