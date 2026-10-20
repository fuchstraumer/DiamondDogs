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


static DisplayInfo k_SPrimaryDisplay;

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
    PlatformSystemImpl* userPtr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));

    const Events::CursorPosEventData eventData{ pos_x, pos_y, window };

    for (auto& posFn : userPtr->Callbacks->CursorPosEvents)
    {
        if (userPtr->Callbacks->CursorPosEventUserData.contains(posFn.hash()))
        {
            posFn(eventData, userPtr->Callbacks->CursorPosEventUserData[posFn.hash()]);
        }
        else
        {
            posFn(eventData, nullptr);
        }
    }
}

void CursorEnterCallback(GLFWwindow* window, int enter)
{
    PlatformSystemImpl* userPtr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    for (auto& enterFn : userPtr->Callbacks->CursorEnterEvents)
    {
        if (userPtr->Callbacks->CursorEnterEventUserData.contains(enterFn.hash()))
        {
            enterFn(enter, userPtr->Callbacks->CursorEnterEventUserData[enterFn.hash()]);
        }
        else
        {
            enterFn(enter, nullptr);
        }
    }
}

void ScrollCallback(GLFWwindow* window, double x_offset, double y_offset)
{
    PlatformSystemImpl* userPtr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::ScrollEventData eventData{ x_offset, y_offset, window };
    for (auto& scrollFn : userPtr->Callbacks->ScrollEvents)
    {
        if (userPtr->Callbacks->ScrollEventUserData.contains(scrollFn.hash()))
        {
            scrollFn(eventData, userPtr->Callbacks->ScrollEventUserData[scrollFn.hash()]);
        }
        else
        {
            scrollFn(eventData, nullptr);
        }
    }
}

void CharCallback(GLFWwindow* window, unsigned int code)
{
    PlatformSystemImpl* userPtr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    for (auto& charFn : userPtr->Callbacks->CharEvents)
    {
        if (userPtr->Callbacks->CharEventUserData.contains(charFn.hash()))
        {
            charFn(code, userPtr->Callbacks->CharEventUserData[charFn.hash()]);
        }
        else
        {
            charFn(code, nullptr);
        }
    }
}

void PathDropCallback(GLFWwindow* window, int count, const char** paths)
{
    PlatformSystemImpl* userPtr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::PathDropEventData eventData{ count, paths, window };
    for (auto& dropFn : userPtr->Callbacks->PathDropEvents)
    {
        if (userPtr->Callbacks->PathDropEventUserData.contains(dropFn.hash()))
        {
            dropFn(eventData, userPtr->Callbacks->PathDropEventUserData[dropFn.hash()]);
        }
        else
        {
            dropFn(eventData, nullptr);
        }
    }
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    PlatformSystemImpl* userPtr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::MouseButtonEventData eventData{ button, action, mods, window };
    for (auto& mouseFn : userPtr->Callbacks->MouseButtonEvents)
    {
        if (userPtr->Callbacks->MouseButtonEventUserData.contains(mouseFn.hash()))
        {
            mouseFn(eventData, userPtr->Callbacks->MouseButtonEventUserData[mouseFn.hash()]);
        }
        else
        {
            mouseFn(eventData, nullptr);
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

    PlatformSystemImpl* userPtr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::KeyboardKeyEventData eventData{ key, scancode, action, mods, window };
    for (auto& keyFn : userPtr->Callbacks->KeyboardKeyEvents)
    {
        if (userPtr->Callbacks->KeyboardKeyEventUserData.contains(keyFn.hash()))
        {
            keyFn(eventData, userPtr->Callbacks->KeyboardKeyEventUserData[keyFn.hash()]);
        }
        else
        {
            keyFn(eventData, nullptr);
        }
    }
}

void ResizeCallback(GLFWwindow* window, int width, int height)
{
    PlatformSystemImpl* userPtr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    const Events::ShouldResizeEventData eventData{ width, height };
    for (auto& resizeFn : userPtr->Callbacks->ShouldResizeEvents)
    {
        if (userPtr->Callbacks->ShouldResizeEventUserData.contains(resizeFn.hash()))
        {
            resizeFn(eventData, userPtr->Callbacks->ShouldResizeEventUserData[resizeFn.hash()]);
        }
        else
        {
            resizeFn(eventData, nullptr);
        }
    }
}

void ShouldCloseCallback(GLFWwindow* window)
{
    PlatformSystemImpl* userPtr = reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    for (auto& closeFn : userPtr->Callbacks->ShouldCloseEvents)
    {
        if (userPtr->Callbacks->ShouldCloseEventUserData.contains(closeFn.hash()))
        {
            closeFn(userPtr->Callbacks->ShouldCloseEventUserData[closeFn.hash()]);
        }
        else
        {
            closeFn(nullptr);
        }
    }
}

PlatformSystemImpl::PlatformSystemImpl(const PlatformWindowCreateInfo& create_info) :
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
    k_SPrimaryDisplay = PlatformWindowSystem::GetPrimaryDisplayInfo();
    if (create_info.DisplayToUse == nullptr && 
        (create_info.DesiredWindowMode == PlatformWindowMode::Fullscreen ||
         create_info.DesiredWindowMode == PlatformWindowMode::FullScreenWindowed))
    {
        ActiveDisplay = k_SPrimaryDisplay;
    }
    else if (create_info.DesiredWindowMode == PlatformWindowMode::Windowed)
    {
        // Set active display to inherit most of the primary display's properties, but override width/height with requested window size
        ActiveDisplay = k_SPrimaryDisplay;
        ActiveDisplay.Width = create_info.InitialWidth;
        ActiveDisplay.Height = create_info.InitialHeight;
    }
    else if (create_info.DesiredWindowMode == PlatformWindowMode::MaximizedWindowed)
    {
        // Set active display to inherit most of the primary display's properties, but override width/height with full display size
        // We'll correct it after creation by querying actual window size from GLFW
        ActiveDisplay = k_SPrimaryDisplay;
        ActiveDisplay.Width = k_SPrimaryDisplay.Width;
        ActiveDisplay.Height = k_SPrimaryDisplay.Height;
    }
    else if (create_info.DisplayToUse != nullptr)
    {
        ActiveDisplay = *create_info.DisplayToUse;
    }

    // Create window with desired settings
    if (create_info.BehaviorFlags.Resizable && 
        create_info.DesiredWindowMode == PlatformWindowMode::Windowed)
    {
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    }
    else
    {
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    }

    if (create_info.BehaviorFlags.Moveable)
    {
    }
    else
    {
    }

    // decorate only if flag set 
    if (create_info.BehaviorFlags.Decorated && 
        (create_info.DesiredWindowMode == PlatformWindowMode::Windowed ||
         create_info.DesiredWindowMode == PlatformWindowMode::MaximizedWindowed))
    {
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    }
    else
    {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }

    // need to also make sure we acquire focus on show if we're going fullscreen
    if (create_info.BehaviorFlags.FocusOnShow || (create_info.DesiredWindowMode == PlatformWindowMode::Fullscreen))
    {
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    }
    else
    {
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    if (create_info.BehaviorFlags.CenterMouse)
    {
        glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_TRUE);
    }
    else
    {
        glfwWindowHint(GLFW_CENTER_CURSOR, GLFW_FALSE);
    }

    glfwWindowHint(GLFW_POSITION_X, static_cast<int>(create_info.InitialPosX));
    glfwWindowHint(GLFW_POSITION_Y, static_cast<int>(create_info.InitialPosY));
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // required to create a Vulkan-capable window, otherwise we create an OpenGL context by default

    switch (create_info.DesiredWindowMode)
    {
    case PlatformWindowMode::Windowed:
        Window = glfwCreateWindow(static_cast<int>(create_info.InitialWidth), static_cast<int>(create_info.InitialHeight), create_info.WindowName, nullptr, nullptr);
        break;
    case PlatformWindowMode::Fullscreen:
        // we only set this particular hint for fullscreen exclusive mode
        glfwWindowHint(GLFW_REFRESH_RATE, static_cast<int>(k_SPrimaryDisplay.RefreshRateCapabilities.RoundedRefreshRate));
        Window = glfwCreateWindow(static_cast<int>(create_info.InitialWidth), static_cast<int>(create_info.InitialHeight), create_info.WindowName, glfwGetPrimaryMonitor(), nullptr);
        break;
    case PlatformWindowMode::FullScreenWindowed:
        // borderless fullscreen windowed mode, set window size to match display size but avoid passing a monitor to avoid going exclusive fullscreen
        Window = glfwCreateWindow(static_cast<int>(ActiveDisplay.Width), static_cast<int>(ActiveDisplay.Height), create_info.WindowName, nullptr, nullptr);
        break;
    case PlatformWindowMode::MaximizedWindowed:
        throw std::domain_error("Oops, I was too lazy to implement this video mode!");
    default:
        throw std::runtime_error("Invalid window mode, can't create GLFW window.");
    };

    if (create_info.DesiredWindowMode == PlatformWindowMode::MaximizedWindowed)
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

void PlatformSystemImpl::AddCursorPosEventListener(CursorPosEvent listener, void* user_data)
{
    Callbacks->CursorPosEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->CursorPosEventUserData[listener.hash()] = user_data;
    }
}

void PlatformSystemImpl::AddCursorEnterEventListener(CursorEnterEvent listener, void* user_data)
{
    Callbacks->CursorEnterEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->CursorEnterEventUserData[listener.hash()] = user_data;
    }
}

void PlatformSystemImpl::AddScrollEventListener(ScrollEvent listener, void* user_data)
{
    Callbacks->ScrollEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->ScrollEventUserData[listener.hash()] = user_data;
    }
}

void PlatformSystemImpl::AddCharEventListener(CharEvent listener, void* user_data)
{
    Callbacks->CharEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->CharEventUserData[listener.hash()] = user_data;
    }
}

void PlatformSystemImpl::AddPathDropEventListener(PathDropEvent listener, void* user_data)
{
    Callbacks->PathDropEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->PathDropEventUserData[listener.hash()] = user_data;
    }
}

void PlatformSystemImpl::AddMouseButtonEventListener(MouseButtonEvent listener, void* user_data)
{
    Callbacks->MouseButtonEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->MouseButtonEventUserData[listener.hash()] = user_data;
    }
}

void PlatformSystemImpl::AddKeyboardKeyEventListener(KeyboardKeyEvent listener, void* user_data)
{
    Callbacks->KeyboardKeyEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->KeyboardKeyEventUserData[listener.hash()] = user_data;
    }
}

void PlatformSystemImpl::AddShouldResizeEventListener(ShouldResizeEvent listener, void* user_data)
{
    Callbacks->ShouldResizeEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->ShouldResizeEventUserData[listener.hash()] = user_data;
    }
}

void PlatformSystemImpl::AddShouldCloseEventListener(ShouldCloseEvent listener, void* user_data)
{
    Callbacks->ShouldCloseEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->ShouldCloseEventUserData[listener.hash()] = user_data;
    }
}
