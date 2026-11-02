#include "InputTypes.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace
{
    constexpr int32_t k_NamedControlsWithoutInvalid = 151;
}

TEST(InputTypes, EventIsThirtyTwoBytesAndTriviallyCopyable)
{
    EXPECT_EQ(sizeof(input::InputEvent), 32u);
    EXPECT_TRUE(std::is_trivially_copyable_v<input::InputEvent>);
}

TEST(InputTypes, EveryNamedControlRoundTripsThroughItsName)
{
    int32_t namedCount = 0;

    for (int32_t value = 1; value < input::k_ControlCount; ++value)
    {
        const input::ControlId control = static_cast<input::ControlId>(value);
        const std::string_view name = input::ToString(control);

        if (name == "Invalid")
        {
            continue;
        }

        ++namedCount;

        const std::optional<input::ControlId> parsed = input::ParseControlId(name);

        ASSERT_TRUE(parsed.has_value()) << name;
        EXPECT_EQ(*parsed, control) << name;
    }

    EXPECT_EQ(namedCount, k_NamedControlsWithoutInvalid);
}

TEST(InputTypes, InvalidRoundTripsAndUnknownNamesAreRejected)
{
    EXPECT_EQ(input::ToString(input::ControlId::Invalid), "Invalid");
    EXPECT_EQ(input::ToString(static_cast<input::ControlId>(0x0300)), "Invalid");
    EXPECT_EQ(input::ParseControlId("Invalid"), input::ControlId::Invalid);
    EXPECT_FALSE(input::ParseControlId("KeyAA").has_value());
    EXPECT_FALSE(input::ParseControlId("keya").has_value());
    EXPECT_FALSE(input::ParseControlId("").has_value());
}

TEST(InputTypes, KeyboardValuesAreUsbHidUsages)
{
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::KeyA), 0x04);
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::Digit1), 0x1E);
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::Enter), 0x28);
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::Space), 0x2C);
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::F1), 0x3A);
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::ArrowRight), 0x4F);
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::ControlLeft), 0xE0);
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::MetaRight), 0xE7);
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::MouseLeft), 0x0100);
    EXPECT_EQ(static_cast<uint16_t>(input::ControlId::GamepadSouth), 0x0200);
}

TEST(InputTypes, DeviceOfFollowsTheControlRanges)
{
    EXPECT_EQ(input::DeviceOf(input::ControlId::KeyW), input::DeviceKind::Keyboard);
    EXPECT_EQ(input::DeviceOf(input::ControlId::WheelY), input::DeviceKind::Mouse);
    EXPECT_EQ(input::DeviceOf(input::ControlId::GamepadRightStickY), input::DeviceKind::Gamepad);
    EXPECT_EQ(input::DeviceOf(input::ControlId::Invalid), input::DeviceKind::Invalid);
}

TEST(InputTypes, SmallEnumsRoundTripThroughNames)
{
    for (int32_t value = 1; value <= static_cast<int32_t>(input::DeviceKind::Pen); ++value)
    {
        const input::DeviceKind device = static_cast<input::DeviceKind>(value);

        EXPECT_EQ(input::ParseDeviceKind(input::ToString(device)), device);
    }

    for (int32_t value = 1; value <= static_cast<int32_t>(input::InputEventKind::FocusLost); ++value)
    {
        const input::InputEventKind kind = static_cast<input::InputEventKind>(value);

        EXPECT_EQ(input::ParseInputEventKind(input::ToString(kind)), kind);
    }

    EXPECT_FALSE(input::ParseDeviceKind("Joystick").has_value());
    EXPECT_FALSE(input::ParseInputEventKind("Scroll").has_value());
}

TEST(InputTypes, FlagOperatorsCombineAndTest)
{
    const input::ModifierFlags both = input::ModifierFlags::Shift | input::ModifierFlags::Alt;

    EXPECT_TRUE(input::Any(both & input::ModifierFlags::Shift));
    EXPECT_FALSE(input::Any(both & input::ModifierFlags::Control));
    EXPECT_FALSE(input::Any(input::EventFlags::Invalid));
    EXPECT_TRUE(input::Any(input::EventFlags::Synthesized));
}
