#include "InputTypes.hpp"
#include "ControlNameTable.hpp"
#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace input
{
    static_assert(std::ranges::is_sorted(k_ControlsByName, {}, &ControlName::Name));
    static_assert(std::ranges::is_sorted(k_ControlsByValue, {}, &ControlName::Id));

    std::string_view ToString(ControlId control) noexcept
    {
        const auto position = std::ranges::lower_bound(k_ControlsByValue, control, {}, &ControlName::Id);

        if (position == k_ControlsByValue.end() || position->Id != control)
        {
            return "Invalid";
        }

        return position->Name;
    }

    std::string_view ToString(DeviceKind device) noexcept
    {
        switch (device)
        {
        case DeviceKind::Keyboard: return "Keyboard";
        case DeviceKind::Mouse: return "Mouse";
        case DeviceKind::Gamepad: return "Gamepad";
        case DeviceKind::Touch: return "Touch";
        case DeviceKind::Pen: return "Pen";
        default: return "Invalid";
        }
    }

    std::string_view ToString(InputEventKind kind) noexcept
    {
        switch (kind)
        {
        case InputEventKind::ButtonDown: return "ButtonDown";
        case InputEventKind::ButtonUp: return "ButtonUp";
        case InputEventKind::AxisValue: return "AxisValue";
        case InputEventKind::AxisDelta: return "AxisDelta";
        case InputEventKind::CursorPosition: return "CursorPosition";
        case InputEventKind::Text: return "Text";
        case InputEventKind::FocusGained: return "FocusGained";
        case InputEventKind::FocusLost: return "FocusLost";
        default: return "Invalid";
        }
    }

    std::optional<ControlId> ParseControlId(std::string_view name) noexcept
    {
        const auto position = std::ranges::lower_bound(k_ControlsByName, name, {}, &ControlName::Name);

        if (position == k_ControlsByName.end() || position->Name != name)
        {
            return std::nullopt;
        }

        return position->Id;
    }

    std::optional<DeviceKind> ParseDeviceKind(std::string_view name) noexcept
    {
        constexpr std::array<DeviceKind, 6> devices
        {
            DeviceKind::Invalid,
            DeviceKind::Keyboard,
            DeviceKind::Mouse,
            DeviceKind::Gamepad,
            DeviceKind::Touch,
            DeviceKind::Pen,
        };

        const auto hasName = [name](DeviceKind device) { return ToString(device) == name; };
        const auto match = std::ranges::find_if(devices, hasName);

        if (match == devices.end())
        {
            return std::nullopt;
        }

        return *match;
    }

    std::optional<InputEventKind> ParseInputEventKind(std::string_view name) noexcept
    {
        constexpr std::array<InputEventKind, 9> kinds
        {
            InputEventKind::Invalid,
            InputEventKind::ButtonDown,
            InputEventKind::ButtonUp,
            InputEventKind::AxisValue,
            InputEventKind::AxisDelta,
            InputEventKind::CursorPosition,
            InputEventKind::Text,
            InputEventKind::FocusGained,
            InputEventKind::FocusLost,
        };

        const auto hasName = [name](InputEventKind kind) { return ToString(kind) == name; };
        const auto match = std::ranges::find_if(kinds, hasName);

        if (match == kinds.end())
        {
            return std::nullopt;
        }

        return *match;
    }

    DeviceKind DeviceOf(ControlId control) noexcept
    {
        const int32_t value = static_cast<int32_t>(control);

        if (value == 0 || value >= k_ControlCount)
        {
            return DeviceKind::Invalid;
        }

        if (value < 0x0100)
        {
            return DeviceKind::Keyboard;
        }

        if (value < 0x0200)
        {
            return DeviceKind::Mouse;
        }

        return DeviceKind::Gamepad;
    }
}
