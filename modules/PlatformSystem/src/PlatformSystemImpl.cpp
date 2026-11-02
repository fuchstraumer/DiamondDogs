#include "PlatformSystem.hpp"
#include "PlatformSystemImpl.hpp"
#include "ImageDataFormats.hpp"
#include "Swapchain.hpp"
#include "PlatformSurface.hpp"
#include <chrono>
#include <cstdint>
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

namespace
{
    struct FramebufferScale
    {
        float X;
        float Y;
    };

    int64_t NowNanoseconds() noexcept;

    FramebufferScale FramebufferScaleOf(GLFWwindow* window) noexcept;

    PlatformSystemImpl& ImplOf(GLFWwindow* window) noexcept;

    void InstallInputCallbacks(GLFWwindow* window) noexcept;

    void InputKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    void InputCharCallback(GLFWwindow* window, unsigned int code_point);

    void InputMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);

    void InputCursorPositionCallback(GLFWwindow* window, double pos_x, double pos_y);

    void InputScrollCallback(GLFWwindow* window, double x_offset, double y_offset);

    void InputFocusCallback(GLFWwindow* window, int focused);

    void InputCursorEnterCallback(GLFWwindow* window, int entered);
}

struct CallbackStorage
{
    // we associate user data pointers with the hash of the delegate function
    using UserDataStorageType = std::unordered_map<size_t, void*>;

    std::vector<ShouldResizeEvent> ShouldResizeEvents;
    UserDataStorageType ShouldResizeEventUserData;
    std::vector<ShouldCloseEvent> ShouldCloseEvents;
    UserDataStorageType ShouldCloseEventUserData;
};

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

    // These go in before any ImGui backend init, because that backend chains the callbacks that exist at that time
    InstallInputCallbacks(Window);
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

void PlatformSystemImpl::SetInputAssembler(input::FrameAssembler* assembler)
{
    const bool focused = Window != nullptr && glfwGetWindowAttrib(Window, GLFW_FOCUSED) != GLFW_FALSE;

    InputSource.SetAssembler(assembler, NowNanoseconds(), focused);
}

void PlatformSystemImpl::AddShouldResizeEventListener(const ShouldResizeEvent& listener, void* user_data)
{
    Callbacks->ShouldResizeEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->ShouldResizeEventUserData[listener.hash()] = user_data;
    }
}

void PlatformSystemImpl::AddShouldCloseEventListener(const ShouldCloseEvent& listener, void* user_data)
{
    Callbacks->ShouldCloseEvents.push_back(listener);
    if (user_data != nullptr)
    {
        Callbacks->ShouldCloseEventUserData[listener.hash()] = user_data;
    }
}

namespace
{
    int64_t NowNanoseconds() noexcept
    {
        const auto sinceEpoch = std::chrono::steady_clock::now().time_since_epoch();

        return std::chrono::duration_cast<std::chrono::nanoseconds>(sinceEpoch).count();
    }

    FramebufferScale FramebufferScaleOf(GLFWwindow* window) noexcept
    {
        int windowWidth = 0;
        int windowHeight = 0;
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetWindowSize(window, &windowWidth, &windowHeight);
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

        FramebufferScale scale{ 1.0f, 1.0f };

        if (windowWidth > 0 && windowHeight > 0)
        {
            scale.X = static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
            scale.Y = static_cast<float>(framebufferHeight) / static_cast<float>(windowHeight);
        }

        return scale;
    }

    PlatformSystemImpl& ImplOf(GLFWwindow* window) noexcept
    {
        return *reinterpret_cast<PlatformSystemImpl*>(glfwGetWindowUserPointer(window));
    }

    void InstallInputCallbacks(GLFWwindow* window) noexcept
    {
        glfwSetKeyCallback(window, InputKeyCallback);
        glfwSetCharCallback(window, InputCharCallback);
        glfwSetMouseButtonCallback(window, InputMouseButtonCallback);
        glfwSetCursorPosCallback(window, InputCursorPositionCallback);
        glfwSetScrollCallback(window, InputScrollCallback);
        glfwSetWindowFocusCallback(window, InputFocusCallback);
        glfwSetCursorEnterCallback(window, InputCursorEnterCallback);
    }

    void InputKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        static_cast<void>(scancode);

        ImplOf(window).InputSource.OnKey(NowNanoseconds(), key, action, mods);
    }

    void InputCharCallback(GLFWwindow* window, unsigned int code_point)
    {
        ImplOf(window).InputSource.OnChar(NowNanoseconds(), code_point);
    }

    void InputMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        ImplOf(window).InputSource.OnMouseButton(NowNanoseconds(), button, action, mods);
    }

    void InputCursorPositionCallback(GLFWwindow* window, double pos_x, double pos_y)
    {
        const FramebufferScale scale = FramebufferScaleOf(window);
        const float framebufferX = static_cast<float>(pos_x) * scale.X;
        const float framebufferY = static_cast<float>(pos_y) * scale.Y;

        ImplOf(window).InputSource.OnCursorPosition(NowNanoseconds(), framebufferX, framebufferY);
    }

    void InputScrollCallback(GLFWwindow* window, double x_offset, double y_offset)
    {
        ImplOf(window).InputSource.OnScroll(
            NowNanoseconds(), static_cast<float>(x_offset), static_cast<float>(y_offset));
    }

    void InputFocusCallback(GLFWwindow* window, int focused)
    {
        ImplOf(window).InputSource.OnFocus(NowNanoseconds(), focused != GLFW_FALSE);
    }

    void InputCursorEnterCallback(GLFWwindow* window, int entered)
    {
        ImplOf(window).InputSource.OnCursorEnter(entered != GLFW_FALSE);
    }
}
