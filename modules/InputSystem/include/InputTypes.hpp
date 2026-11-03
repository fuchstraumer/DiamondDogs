#pragma once
#ifndef DIAMOND_DOGS_INPUT_SYSTEM_INPUT_TYPES_HPP
#define DIAMOND_DOGS_INPUT_SYSTEM_INPUT_TYPES_HPP
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace input
{
    /** @brief Identifies one control in a single space. Keyboard values are USB HID usage page 0x07 usage IDs. */
    enum class ControlId : uint16_t
    {
        Invalid = 0x00,
        KeyA = 0x04,
        KeyB = 0x05,
        KeyC = 0x06,
        KeyD = 0x07,
        KeyE = 0x08,
        KeyF = 0x09,
        KeyG = 0x0A,
        KeyH = 0x0B,
        KeyI = 0x0C,
        KeyJ = 0x0D,
        KeyK = 0x0E,
        KeyL = 0x0F,
        KeyM = 0x10,
        KeyN = 0x11,
        KeyO = 0x12,
        KeyP = 0x13,
        KeyQ = 0x14,
        KeyR = 0x15,
        KeyS = 0x16,
        KeyT = 0x17,
        KeyU = 0x18,
        KeyV = 0x19,
        KeyW = 0x1A,
        KeyX = 0x1B,
        KeyY = 0x1C,
        KeyZ = 0x1D,
        Digit1 = 0x1E,
        Digit2 = 0x1F,
        Digit3 = 0x20,
        Digit4 = 0x21,
        Digit5 = 0x22,
        Digit6 = 0x23,
        Digit7 = 0x24,
        Digit8 = 0x25,
        Digit9 = 0x26,
        Digit0 = 0x27,
        Enter = 0x28,
        Escape = 0x29,
        Backspace = 0x2A,
        Tab = 0x2B,
        Space = 0x2C,
        Minus = 0x2D,
        Equal = 0x2E,
        BracketLeft = 0x2F,
        BracketRight = 0x30,
        Backslash = 0x31,
        Semicolon = 0x33,
        Quote = 0x34,
        Backquote = 0x35,
        Comma = 0x36,
        Period = 0x37,
        Slash = 0x38,
        CapsLock = 0x39,
        F1 = 0x3A,
        F2 = 0x3B,
        F3 = 0x3C,
        F4 = 0x3D,
        F5 = 0x3E,
        F6 = 0x3F,
        F7 = 0x40,
        F8 = 0x41,
        F9 = 0x42,
        F10 = 0x43,
        F11 = 0x44,
        F12 = 0x45,
        PrintScreen = 0x46,
        ScrollLock = 0x47,
        Pause = 0x48,
        Insert = 0x49,
        Home = 0x4A,
        PageUp = 0x4B,
        Delete = 0x4C,
        End = 0x4D,
        PageDown = 0x4E,
        ArrowRight = 0x4F,
        ArrowLeft = 0x50,
        ArrowDown = 0x51,
        ArrowUp = 0x52,
        NumLock = 0x53,
        NumpadDivide = 0x54,
        NumpadMultiply = 0x55,
        NumpadSubtract = 0x56,
        NumpadAdd = 0x57,
        NumpadEnter = 0x58,
        Numpad1 = 0x59,
        Numpad2 = 0x5A,
        Numpad3 = 0x5B,
        Numpad4 = 0x5C,
        Numpad5 = 0x5D,
        Numpad6 = 0x5E,
        Numpad7 = 0x5F,
        Numpad8 = 0x60,
        Numpad9 = 0x61,
        Numpad0 = 0x62,
        NumpadDecimal = 0x63,
        IntlBackslash = 0x64,
        ContextMenu = 0x65,
        NumpadEqual = 0x67,
        F13 = 0x68,
        F14 = 0x69,
        F15 = 0x6A,
        F16 = 0x6B,
        F17 = 0x6C,
        F18 = 0x6D,
        F19 = 0x6E,
        F20 = 0x6F,
        F21 = 0x70,
        F22 = 0x71,
        F23 = 0x72,
        F24 = 0x73,
        NumpadComma = 0x85,
        IntlRo = 0x87,
        IntlYen = 0x89,
        ControlLeft = 0xE0,
        ShiftLeft = 0xE1,
        AltLeft = 0xE2,
        MetaLeft = 0xE3,
        ControlRight = 0xE4,
        ShiftRight = 0xE5,
        AltRight = 0xE6,
        MetaRight = 0xE7,
        MouseLeft = 0x0100,
        MouseRight = 0x0101,
        MouseMiddle = 0x0102,
        MouseBack = 0x0103,
        MouseForward = 0x0104,
        MouseX = 0x0105,
        MouseY = 0x0106,
        WheelX = 0x0107,
        WheelY = 0x0108,
        GamepadSouth = 0x0200,
        GamepadEast = 0x0201,
        GamepadWest = 0x0202,
        GamepadNorth = 0x0203,
        GamepadLeftShoulder = 0x0204,
        GamepadRightShoulder = 0x0205,
        GamepadView = 0x0206,
        GamepadMenu = 0x0207,
        GamepadGuide = 0x0208,
        GamepadLeftStickPress = 0x0209,
        GamepadRightStickPress = 0x020A,
        GamepadDPadUp = 0x020B,
        GamepadDPadDown = 0x020C,
        GamepadDPadLeft = 0x020D,
        GamepadDPadRight = 0x020E,
        GamepadLeftTrigger = 0x020F,
        GamepadRightTrigger = 0x0210,
        GamepadLeftStickX = 0x0211,
        GamepadLeftStickY = 0x0212,
        GamepadRightStickX = 0x0213,
        GamepadRightStickY = 0x0214,
    };

    inline constexpr int32_t k_ControlCount = 0x0300;

    enum class DeviceKind : uint8_t
    {
        Invalid = 0,
        Keyboard,
        Mouse,
        Gamepad,
        Touch,
        Pen,
    };

    enum class InputEventKind : uint8_t
    {
        Invalid = 0,
        ButtonDown,
        ButtonUp,
        /** @brief An absolute value in X, such as a stick or a trigger. */
        AxisValue,
        /** @brief A relative change in X, such as mouse motion or the wheel, in notches. */
        AxisDelta,
        /** @brief The cursor position in framebuffer pixels from the top left, in X and Y. */
        CursorPosition,
        /** @brief A Unicode code point in CodePoint. */
        Text,
        FocusGained,
        FocusLost,
    };

    enum class ModifierFlags : uint8_t
    {
        Invalid = 0,
        Shift = 1 << 0,
        Control = 1 << 1,
        Alt = 1 << 2,
        Super = 1 << 3,
        CapsLock = 1 << 4,
        NumLock = 1 << 5,
    };

    enum class EventFlags : uint8_t
    {
        Invalid = 0,
        Synthesized = 1 << 0,
    };

    /** @brief A raw input record. The platform source fills it, and FrameAssembler consumes it. */
    struct InputEvent
    {
        int64_t TimestampNs;
        float X;
        float Y;
        uint32_t CodePoint;
        ControlId Control;
        InputEventKind Kind;
        DeviceKind Device;
        uint8_t DeviceIndex;
        ModifierFlags Modifiers;
        EventFlags Flags;
    };

    static_assert(sizeof(InputEvent) == 32);
    static_assert(std::is_trivially_copyable_v<InputEvent>);

    constexpr ModifierFlags operator|(ModifierFlags left, ModifierFlags right) noexcept
    {
        return static_cast<ModifierFlags>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    constexpr ModifierFlags operator&(ModifierFlags left, ModifierFlags right) noexcept
    {
        return static_cast<ModifierFlags>(static_cast<uint8_t>(left) & static_cast<uint8_t>(right));
    }

    constexpr ModifierFlags& operator|=(ModifierFlags& left, ModifierFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    constexpr EventFlags operator|(EventFlags left, EventFlags right) noexcept
    {
        return static_cast<EventFlags>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
    }

    constexpr EventFlags operator&(EventFlags left, EventFlags right) noexcept
    {
        return static_cast<EventFlags>(static_cast<uint8_t>(left) & static_cast<uint8_t>(right));
    }

    constexpr EventFlags& operator|=(EventFlags& left, EventFlags right) noexcept
    {
        left = left | right;
        return left;
    }

    /** @brief Returns true when any bit of the value is set. */
    constexpr bool Any(ModifierFlags value) noexcept
    {
        return static_cast<uint8_t>(value) != 0;
    }

    /** @brief Returns true when any bit of the value is set. */
    constexpr bool Any(EventFlags value) noexcept
    {
        return static_cast<uint8_t>(value) != 0;
    }

    /** @brief Returns the enumerator name, or "Invalid" for a value that has no name. */
    [[nodiscard]] std::string_view ToString(ControlId control) noexcept;

    [[nodiscard]] std::string_view ToString(DeviceKind device) noexcept;

    [[nodiscard]] std::string_view ToString(InputEventKind kind) noexcept;

    /** @brief Returns the control with this enumerator name, or nothing when the name is unknown. */
    [[nodiscard]] std::optional<ControlId> ParseControlId(std::string_view name) noexcept;

    [[nodiscard]] std::optional<DeviceKind> ParseDeviceKind(std::string_view name) noexcept;

    [[nodiscard]] std::optional<InputEventKind> ParseInputEventKind(std::string_view name) noexcept;

    /** @brief Returns the device class that a control belongs to, or Invalid for the Invalid control and for a value outside the control space. */
    [[nodiscard]] DeviceKind DeviceOf(ControlId control) noexcept;
}

#endif // !DIAMOND_DOGS_INPUT_SYSTEM_INPUT_TYPES_HPP
