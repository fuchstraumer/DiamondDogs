#include "InputLog.hpp"
#include "InputTypes.hpp"
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::string_view k_GoodLine =
        "{\"t\":5,\"kind\":\"ButtonDown\",\"control\":\"KeyW\",\"device\":\"Keyboard\",\"index\":0,\"mods\":0,"
        "\"flags\":0,\"x\":0,\"y\":0,\"cp\":0}";

    void ExpectSameEvent(const input::InputEvent& expected, const input::InputEvent& actual)
    {
        EXPECT_EQ(expected.TimestampNs, actual.TimestampNs);
        EXPECT_EQ(std::bit_cast<uint32_t>(expected.X), std::bit_cast<uint32_t>(actual.X));
        EXPECT_EQ(std::bit_cast<uint32_t>(expected.Y), std::bit_cast<uint32_t>(actual.Y));
        EXPECT_EQ(expected.CodePoint, actual.CodePoint);
        EXPECT_EQ(expected.Control, actual.Control);
        EXPECT_EQ(expected.Kind, actual.Kind);
        EXPECT_EQ(expected.Device, actual.Device);
        EXPECT_EQ(expected.DeviceIndex, actual.DeviceIndex);
        EXPECT_EQ(expected.Modifiers, actual.Modifiers);
        EXPECT_EQ(expected.Flags, actual.Flags);
    }

    input::InputLogError ReadError(const std::string& text)
    {
        const input::InputLogResult<std::vector<input::InputEvent>> result = input::ReadInputLog(text);

        if (result)
        {
            return input::InputLogError::Success;
        }

        return result.error();
    }

    // replaces the first occurrence of a fragment in the good line
    std::string GoodLineWith(std::string_view fragment, std::string_view replacement)
    {
        std::string line{ k_GoodLine };
        const size_t position = line.find(fragment);
        line.replace(position, fragment.size(), replacement);

        return line;
    }
}

TEST(InputLog, WriteThenReadReturnsIdenticalEvents)
{
    std::array<input::InputEvent, 5> events{};

    events[0].TimestampNs = 123456;
    events[0].Kind = input::InputEventKind::ButtonDown;
    events[0].Control = input::ControlId::KeyW;
    events[0].Device = input::DeviceKind::Keyboard;
    events[0].Modifiers = input::ModifierFlags::Shift | input::ModifierFlags::NumLock;

    events[1].TimestampNs = -42;
    events[1].Kind = input::InputEventKind::AxisDelta;
    events[1].Control = input::ControlId::MouseX;
    events[1].Device = input::DeviceKind::Mouse;
    events[1].X = 0.1f;
    events[1].Y = -1e-7f;

    events[2].TimestampNs = INT64_MAX;
    events[2].Kind = input::InputEventKind::CursorPosition;
    events[2].Device = input::DeviceKind::Mouse;
    events[2].X = 1e30f;
    events[2].Y = 16777217.0f;

    events[3].Kind = input::InputEventKind::Text;
    events[3].CodePoint = 0x1F600;

    events[4].TimestampNs = 7;
    events[4].Kind = input::InputEventKind::ButtonUp;
    events[4].Control = input::ControlId::GamepadSouth;
    events[4].Device = input::DeviceKind::Gamepad;
    events[4].DeviceIndex = 3;
    events[4].Flags = input::EventFlags::Synthesized;
    events[4].X = -0.0f;

    const std::string text = input::WriteInputLog(events);
    const input::InputLogResult<std::vector<input::InputEvent>> result = input::ReadInputLog(text);

    ASSERT_TRUE(result.has_value()) << input::ToString(result.error());
    ASSERT_EQ(result->size(), events.size());

    for (size_t eventIndex = 0; eventIndex < events.size(); ++eventIndex)
    {
        ExpectSameEvent(events[eventIndex], (*result)[eventIndex]);
    }
}

TEST(InputLog, WriterUsesFixedKeyOrderAndOneLinePerEvent)
{
    std::array<input::InputEvent, 1> events{};
    events[0].TimestampNs = 5;
    events[0].Kind = input::InputEventKind::ButtonDown;
    events[0].Control = input::ControlId::KeyW;
    events[0].Device = input::DeviceKind::Keyboard;

    const std::string text = input::WriteInputLog(events);

    EXPECT_EQ(text, std::string{ k_GoodLine } + "\n");
}

TEST(InputLog, ReaderSkipsBlankLinesAndCarriageReturns)
{
    const std::string text = "\n  \n" + std::string{ k_GoodLine } + "\r\n\n" + std::string{ k_GoodLine } + "\n";
    const input::InputLogResult<std::vector<input::InputEvent>> result = input::ReadInputLog(text);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), 2u);
}

TEST(InputLog, EmptyTextGivesNoEvents)
{
    const input::InputLogResult<std::vector<input::InputEvent>> result = input::ReadInputLog("");

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->empty());
}

TEST(InputLog, UnknownControlNameIsRejected)
{
    EXPECT_EQ(ReadError(GoodLineWith("KeyW", "KeyWW")), input::InputLogError::UnknownControl);
}

TEST(InputLog, UnknownKindNameIsRejected)
{
    EXPECT_EQ(ReadError(GoodLineWith("ButtonDown", "Tap")), input::InputLogError::UnknownKind);
}

TEST(InputLog, UnknownDeviceNameIsRejected)
{
    EXPECT_EQ(ReadError(GoodLineWith("Keyboard", "Joystick")), input::InputLogError::UnknownDevice);
}

TEST(InputLog, MalformedNumbersAreRejected)
{
    EXPECT_EQ(ReadError(GoodLineWith("\"t\":5", "\"t\":5x")), input::InputLogError::MalformedNumber);
    EXPECT_EQ(ReadError(GoodLineWith("\"x\":0", "\"x\":zero")), input::InputLogError::MalformedNumber);
    EXPECT_EQ(ReadError(GoodLineWith("\"index\":0", "\"index\":256")), input::InputLogError::MalformedNumber);
    EXPECT_EQ(ReadError(GoodLineWith("\"cp\":0", "\"cp\":1114112")), input::InputLogError::MalformedNumber);
    EXPECT_EQ(ReadError(GoodLineWith("\"mods\":0", "\"mods\":")), input::InputLogError::MalformedNumber);
}

TEST(InputLog, MissingKeyIsRejected)
{
    EXPECT_EQ(ReadError(GoodLineWith("\"flags\":0,", "")), input::InputLogError::MissingKey);
    EXPECT_EQ(ReadError(GoodLineWith("\"control\":\"KeyW\",", "")), input::InputLogError::MissingKey);
}

TEST(InputLog, LineThatIsNotAnObjectIsRejected)
{
    EXPECT_EQ(ReadError("not json"), input::InputLogError::MalformedLine);
    EXPECT_EQ(ReadError(GoodLineWith("\"KeyW\"", "KeyW")), input::InputLogError::MalformedLine);
}

TEST(InputLog, ErrorNamesAreDistinct)
{
    EXPECT_EQ(input::ToString(input::InputLogError::MissingKey), "MissingKey");
    EXPECT_EQ(input::ToString(input::InputLogError::Success), "Success");
}

TEST(InputLog, UnnamedControlsRoundTripAsBareIntegers)
{
    std::array<input::InputEvent, 3> events{};

    events[0].Kind = input::InputEventKind::ButtonDown;
    events[0].Control = static_cast<input::ControlId>(0x0032);
    events[0].Device = input::DeviceKind::Keyboard;

    events[1].Kind = input::InputEventKind::ButtonUp;
    events[1].Control = static_cast<input::ControlId>(0x0150);
    events[1].Device = input::DeviceKind::Mouse;

    events[2].Kind = input::InputEventKind::ButtonDown;
    events[2].Control = input::ControlId::KeyW;
    events[2].Device = input::DeviceKind::Keyboard;

    const std::string text = input::WriteInputLog(events);

    EXPECT_NE(text.find("\"control\":50,"), std::string::npos);
    EXPECT_NE(text.find("\"control\":336,"), std::string::npos);
    EXPECT_NE(text.find("\"control\":\"KeyW\","), std::string::npos);

    const input::InputLogResult<std::vector<input::InputEvent>> result = input::ReadInputLog(text);

    ASSERT_TRUE(result.has_value()) << input::ToString(result.error());
    ASSERT_EQ(result->size(), events.size());

    for (size_t eventIndex = 0; eventIndex < events.size(); ++eventIndex)
    {
        ExpectSameEvent(events[eventIndex], (*result)[eventIndex]);
    }
}

TEST(InputLog, InvalidControlStaysQuoted)
{
    std::array<input::InputEvent, 1> events{};
    events[0].Kind = input::InputEventKind::FocusGained;

    const std::string text = input::WriteInputLog(events);

    EXPECT_NE(text.find("\"control\":\"Invalid\","), std::string::npos);
}

TEST(InputLog, ControlIntegerOutsideTheSpaceIsRejected)
{
    EXPECT_EQ(ReadError(GoodLineWith("\"KeyW\"", "768")), input::InputLogError::MalformedNumber);
    EXPECT_EQ(ReadError(GoodLineWith("\"KeyW\"", "-1")), input::InputLogError::MalformedNumber);
    EXPECT_EQ(ReadError(GoodLineWith("\"KeyW\"", "767")), input::InputLogError::Success);
}

TEST(InputLog, UnknownQuotedControlNameIsStillRejected)
{
    EXPECT_EQ(ReadError(GoodLineWith("KeyW", "NoSuchKey")), input::InputLogError::UnknownControl);
}
