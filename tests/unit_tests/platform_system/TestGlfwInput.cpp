#include "GlfwInputSource.hpp"
#include "GlfwInputTranslation.hpp"
#include "InputFrame.hpp"
#include "InputTypes.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <map>
#include <span>

#include "GLFW/glfw3.h"

namespace
{
    constexpr int32_t k_EventCapacity = 64;
    constexpr int32_t k_TextCapacity = 16;

    struct SourceFixture
    {
        SourceFixture() :
          assembler(k_EventCapacity, k_TextCapacity)
        {
            source.SetAssembler(&assembler, 0, true);
            assembler.StartFrame();
        }

        const input::InputFrame& Finish()
        {
            return assembler.FinishFrame();
        }

        input::FrameAssembler assembler;
        GlfwInputSource source;
    };
}

TEST(GlfwInputTranslation, KeySamplesMapToTheirHidControls)
{
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_A), input::ControlId::KeyA);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_Z), input::ControlId::KeyZ);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_0), input::ControlId::Digit0);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_9), input::ControlId::Digit9);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_F1), input::ControlId::F1);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_F12), input::ControlId::F12);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_ESCAPE), input::ControlId::Escape);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_ENTER), input::ControlId::Enter);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_SPACE), input::ControlId::Space);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_UP), input::ControlId::ArrowUp);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_DOWN), input::ControlId::ArrowDown);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_LEFT), input::ControlId::ArrowLeft);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_RIGHT), input::ControlId::ArrowRight);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_LEFT_SHIFT), input::ControlId::ShiftLeft);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_RIGHT_SHIFT), input::ControlId::ShiftRight);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_LEFT_CONTROL), input::ControlId::ControlLeft);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_RIGHT_CONTROL), input::ControlId::ControlRight);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_LEFT_ALT), input::ControlId::AltLeft);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_RIGHT_ALT), input::ControlId::AltRight);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_LEFT_SUPER), input::ControlId::MetaLeft);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_RIGHT_SUPER), input::ControlId::MetaRight);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_KP_0), input::ControlId::Numpad0);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_KP_ENTER), input::ControlId::NumpadEnter);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_GRAVE_ACCENT), input::ControlId::Backquote);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_WORLD_1), input::ControlId::IntlBackslash);
}

TEST(GlfwInputTranslation, UnknownAndUnmappedKeysGiveInvalid)
{
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_UNKNOWN), input::ControlId::Invalid);
    EXPECT_EQ(ControlFromGlfwKey(GLFW_KEY_F25), input::ControlId::Invalid);
    EXPECT_EQ(ControlFromGlfwKey(100000), input::ControlId::Invalid);
}

TEST(GlfwInputTranslation, EveryKeyIsInvalidOrAKeyboardControlAndNoControlRepeats)
{
    std::map<input::ControlId, int> firstKeyOfControl;

    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key)
    {
        const input::ControlId control = ControlFromGlfwKey(key);

        if (control == input::ControlId::Invalid)
        {
            continue;
        }

        EXPECT_EQ(input::DeviceOf(control), input::DeviceKind::Keyboard) << "GLFW key " << key;

        const auto [entry, inserted] = firstKeyOfControl.try_emplace(control, key);

        // WORLD_1 and WORLD_2 are the same physical key, reported under a different token on each platform
        const bool isWorldKeyPair = key == GLFW_KEY_WORLD_2 && entry->second == GLFW_KEY_WORLD_1;

        EXPECT_TRUE(inserted || isWorldKeyPair)
            << "GLFW keys " << entry->second << " and " << key << " both map to " << input::ToString(control);
    }
}

TEST(GlfwInputTranslation, MouseButtonsMapInOrder)
{
    EXPECT_EQ(ControlFromGlfwMouseButton(0), input::ControlId::MouseLeft);
    EXPECT_EQ(ControlFromGlfwMouseButton(1), input::ControlId::MouseRight);
    EXPECT_EQ(ControlFromGlfwMouseButton(2), input::ControlId::MouseMiddle);
    EXPECT_EQ(ControlFromGlfwMouseButton(3), input::ControlId::MouseBack);
    EXPECT_EQ(ControlFromGlfwMouseButton(4), input::ControlId::MouseForward);
    EXPECT_EQ(ControlFromGlfwMouseButton(5), input::ControlId::Invalid);
    EXPECT_EQ(ControlFromGlfwMouseButton(-1), input::ControlId::Invalid);
}

TEST(GlfwInputTranslation, ModifiersMapBitForBit)
{
    EXPECT_EQ(ModifiersFromGlfw(0), input::ModifierFlags::Invalid);
    EXPECT_EQ(ModifiersFromGlfw(GLFW_MOD_SHIFT), input::ModifierFlags::Shift);
    EXPECT_EQ(ModifiersFromGlfw(GLFW_MOD_CONTROL), input::ModifierFlags::Control);
    EXPECT_EQ(ModifiersFromGlfw(GLFW_MOD_ALT), input::ModifierFlags::Alt);
    EXPECT_EQ(ModifiersFromGlfw(GLFW_MOD_SUPER), input::ModifierFlags::Super);
    EXPECT_EQ(ModifiersFromGlfw(GLFW_MOD_CAPS_LOCK), input::ModifierFlags::CapsLock);
    EXPECT_EQ(ModifiersFromGlfw(GLFW_MOD_NUM_LOCK), input::ModifierFlags::NumLock);

    const input::ModifierFlags shiftAndAlt = input::ModifierFlags::Shift | input::ModifierFlags::Alt;

    EXPECT_EQ(ModifiersFromGlfw(GLFW_MOD_SHIFT | GLFW_MOD_ALT), shiftAndAlt);
}

TEST(GlfwInputSource, PressRepeatAndReleaseGiveOnePressEdgeAndOneReleaseEdge)
{
    SourceFixture fixture;
    fixture.source.OnKey(100, GLFW_KEY_W, GLFW_PRESS, 0);
    fixture.source.OnKey(200, GLFW_KEY_W, GLFW_REPEAT, 0);

    const input::InputFrame& pressFrame = fixture.Finish();
    const input::ControlState& heldState = pressFrame.State(input::ControlId::KeyW);

    EXPECT_TRUE(heldState.Down);
    EXPECT_TRUE(heldState.PressedThisFrame);
    EXPECT_FALSE(heldState.ReleasedThisFrame);
    EXPECT_EQ(heldState.LastChangeNs, 100);

    fixture.assembler.StartFrame();
    fixture.source.OnKey(300, GLFW_KEY_W, GLFW_RELEASE, 0);

    const input::InputFrame& releaseFrame = fixture.Finish();
    const input::ControlState& releasedState = releaseFrame.State(input::ControlId::KeyW);

    EXPECT_FALSE(releasedState.Down);
    EXPECT_FALSE(releasedState.PressedThisFrame);
    EXPECT_TRUE(releasedState.ReleasedThisFrame);
    EXPECT_EQ(releasedState.LastChangeNs, 300);
}

TEST(GlfwInputSource, KeyEventsCarryTheKeyboardDeviceAndTheModifiers)
{
    SourceFixture fixture;
    fixture.source.OnKey(100, GLFW_KEY_A, GLFW_PRESS, GLFW_MOD_SHIFT | GLFW_MOD_CONTROL);

    const input::InputFrame& frame = fixture.Finish();
    std::span<const input::InputEvent> events = frame.Events();

    ASSERT_EQ(events.size(), 1u);

    const input::InputEvent& event = events[0];

    EXPECT_EQ(event.Kind, input::InputEventKind::ButtonDown);
    EXPECT_EQ(event.Control, input::ControlId::KeyA);
    EXPECT_EQ(event.Device, input::DeviceKind::Keyboard);
    EXPECT_EQ(event.Modifiers, input::ModifierFlags::Shift | input::ModifierFlags::Control);
    EXPECT_EQ(event.TimestampNs, 100);
}

TEST(GlfwInputSource, UnmappedKeysMakeNoEvent)
{
    SourceFixture fixture;
    const int64_t eventCountBefore = std::ssize(fixture.Finish().Events());

    fixture.source.OnKey(100, GLFW_KEY_UNKNOWN, GLFW_PRESS, 0);
    fixture.source.OnKey(110, GLFW_KEY_F25, GLFW_PRESS, 0);

    const int64_t eventCountAfter = std::ssize(fixture.Finish().Events());

    EXPECT_EQ(eventCountBefore, eventCountAfter);
}

TEST(GlfwInputSource, CodePointAboveTheBasicPlaneArrivesWhole)
{
    SourceFixture fixture;
    fixture.source.OnChar(100, 0x1F600);
    fixture.source.OnChar(110, 'a');

    const input::InputFrame& frame = fixture.Finish();
    std::span<const uint32_t> text = frame.Text();

    ASSERT_EQ(text.size(), 2u);
    EXPECT_EQ(text[0], 0x1F600u);
    EXPECT_EQ(text[1], static_cast<uint32_t>('a'));
}

TEST(GlfwInputSource, MouseButtonsGiveEdgesDevicesAndModifiers)
{
    SourceFixture fixture;
    fixture.source.OnMouseButton(100, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, GLFW_MOD_ALT);
    fixture.source.OnMouseButton(110, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS, 0);
    fixture.source.OnMouseButton(120, 7, GLFW_PRESS, 0);

    const input::InputFrame& frame = fixture.Finish();
    std::span<const input::InputEvent> events = frame.Events();

    ASSERT_EQ(events.size(), 2u);

    const input::InputEvent& rightPress = events[0];

    EXPECT_EQ(rightPress.Control, input::ControlId::MouseRight);
    EXPECT_EQ(rightPress.Device, input::DeviceKind::Mouse);
    EXPECT_EQ(rightPress.Modifiers, input::ModifierFlags::Alt);

    EXPECT_TRUE(frame.State(input::ControlId::MouseRight).PressedThisFrame);
    EXPECT_TRUE(frame.State(input::ControlId::MouseMiddle).PressedThisFrame);
    EXPECT_FALSE(frame.State(input::ControlId::MouseLeft).Down);

    fixture.assembler.StartFrame();
    fixture.source.OnMouseButton(200, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, 0);

    const input::InputFrame& releaseFrame = fixture.Finish();

    EXPECT_TRUE(releaseFrame.State(input::ControlId::MouseRight).ReleasedThisFrame);
    EXPECT_TRUE(releaseFrame.State(input::ControlId::MouseMiddle).Down);
}

TEST(GlfwInputSource, CursorMovesGiveDeltasAndTheFirstMoveGivesNone)
{
    SourceFixture fixture;
    fixture.source.OnCursorPosition(100, 10.0f, 20.0f);

    const input::InputFrame& firstFrame = fixture.Finish();

    EXPECT_FLOAT_EQ(firstFrame.CursorX(), 10.0f);
    EXPECT_FLOAT_EQ(firstFrame.CursorY(), 20.0f);
    EXPECT_FLOAT_EQ(firstFrame.State(input::ControlId::MouseX).Delta, 0.0f);
    EXPECT_FLOAT_EQ(firstFrame.State(input::ControlId::MouseY).Delta, 0.0f);

    fixture.assembler.StartFrame();
    fixture.source.OnCursorPosition(200, 13.0f, 18.0f);
    fixture.source.OnCursorPosition(300, 14.0f, 18.0f);

    const input::InputFrame& secondFrame = fixture.Finish();

    EXPECT_FLOAT_EQ(secondFrame.CursorX(), 14.0f);
    EXPECT_FLOAT_EQ(secondFrame.CursorY(), 18.0f);
    EXPECT_FLOAT_EQ(secondFrame.State(input::ControlId::MouseX).Delta, 4.0f);
    EXPECT_FLOAT_EQ(secondFrame.State(input::ControlId::MouseY).Delta, -2.0f);
}

TEST(GlfwInputSource, DeltaEventsUseTheMouseDeviceAndOnlyTheXField)
{
    SourceFixture fixture;
    fixture.source.OnCursorPosition(100, 10.0f, 20.0f);
    fixture.source.OnCursorPosition(200, 12.0f, 25.0f);

    const input::InputFrame& frame = fixture.Finish();
    std::span<const input::InputEvent> events = frame.Events();

    // position, position, MouseX delta, MouseY delta
    ASSERT_EQ(events.size(), 4u);

    const input::InputEvent& deltaX = events[2];
    const input::InputEvent& deltaY = events[3];

    EXPECT_EQ(deltaX.Kind, input::InputEventKind::AxisDelta);
    EXPECT_EQ(deltaX.Control, input::ControlId::MouseX);
    EXPECT_EQ(deltaX.Device, input::DeviceKind::Mouse);
    EXPECT_FLOAT_EQ(deltaX.X, 2.0f);
    EXPECT_FLOAT_EQ(deltaX.Y, 0.0f);

    EXPECT_EQ(deltaY.Control, input::ControlId::MouseY);
    EXPECT_FLOAT_EQ(deltaY.X, 5.0f);
    EXPECT_FLOAT_EQ(deltaY.Y, 0.0f);
}

TEST(GlfwInputSource, CursorEnterAndFocusGainResetTheDeltaBaseline)
{
    SourceFixture fixture;
    fixture.source.OnCursorPosition(100, 10.0f, 10.0f);
    fixture.source.OnCursorEnter(true);
    fixture.source.OnCursorPosition(200, 500.0f, 400.0f);

    const input::InputFrame& enterFrame = fixture.Finish();

    EXPECT_FLOAT_EQ(enterFrame.State(input::ControlId::MouseX).Delta, 0.0f);
    EXPECT_FLOAT_EQ(enterFrame.State(input::ControlId::MouseY).Delta, 0.0f);

    fixture.assembler.StartFrame();
    fixture.source.OnFocus(300, true);
    fixture.source.OnCursorPosition(400, 900.0f, 700.0f);

    const input::InputFrame& focusFrame = fixture.Finish();

    EXPECT_FLOAT_EQ(focusFrame.State(input::ControlId::MouseX).Delta, 0.0f);
    EXPECT_FLOAT_EQ(focusFrame.State(input::ControlId::MouseY).Delta, 0.0f);

    fixture.assembler.StartFrame();
    fixture.source.OnCursorPosition(500, 901.0f, 700.0f);

    const input::InputFrame& moveFrame = fixture.Finish();

    EXPECT_FLOAT_EQ(moveFrame.State(input::ControlId::MouseX).Delta, 1.0f);
}

TEST(GlfwInputSource, ScrollSignsFollowTheModuleConvention)
{
    SourceFixture fixture;

    // GLFW positive y is away from the user. GLFW positive x is left, so WheelX flips it
    fixture.source.OnScroll(100, 1.0f, 2.0f);

    const input::InputFrame& frame = fixture.Finish();

    EXPECT_FLOAT_EQ(frame.State(input::ControlId::WheelY).Delta, 2.0f);
    EXPECT_FLOAT_EQ(frame.State(input::ControlId::WheelX).Delta, -1.0f);

    fixture.assembler.StartFrame();
    fixture.source.OnScroll(200, -0.5f, 0.0f);

    const input::InputFrame& secondFrame = fixture.Finish();
    std::span<const input::InputEvent> events = secondFrame.Events();

    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].Control, input::ControlId::WheelX);
    EXPECT_EQ(events[0].Device, input::DeviceKind::Mouse);
    EXPECT_FLOAT_EQ(events[0].X, 0.5f);
}

TEST(GlfwInputSource, SetAssemblerPostsTheRealFocusState)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    GlfwInputSource source;
    assembler.StartFrame();
    source.SetAssembler(&assembler, 50, false);

    const input::InputFrame& unfocusedFrame = assembler.FinishFrame();

    EXPECT_FALSE(unfocusedFrame.HasFocus());

    assembler.StartFrame();
    source.SetAssembler(&assembler, 60, true);

    const input::InputFrame& focusedFrame = assembler.FinishFrame();

    EXPECT_TRUE(focusedFrame.HasFocus());
}

TEST(GlfwInputSource, NoAssemblerDiscardsEveryEventWithoutACrash)
{
    GlfwInputSource source;
    source.OnKey(100, GLFW_KEY_A, GLFW_PRESS, 0);
    source.OnChar(110, 0x1F600);
    source.OnMouseButton(120, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    source.OnCursorPosition(130, 1.0f, 2.0f);
    source.OnCursorPosition(140, 3.0f, 4.0f);
    source.OnScroll(150, 1.0f, 1.0f);
    source.OnFocus(160, false);
    source.OnCursorEnter(true);

    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    source.SetAssembler(&assembler, 170, true);
    assembler.StartFrame();

    const input::InputFrame& frame = assembler.FinishFrame();

    EXPECT_EQ(frame.Events().size(), 0u);
    EXPECT_FALSE(frame.State(input::ControlId::KeyA).Down);

    source.SetAssembler(nullptr, 180, true);
    source.OnKey(190, GLFW_KEY_B, GLFW_PRESS, 0);

    const input::InputFrame& afterNull = assembler.FinishFrame();

    EXPECT_FALSE(afterNull.State(input::ControlId::KeyB).Down);
    EXPECT_EQ(afterNull.Events().size(), 0u);
}
