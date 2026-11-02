#include "InputFrame.hpp"
#include "InputTypes.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>

namespace
{
    constexpr int32_t k_EventCapacity = 64;
    constexpr int32_t k_TextCapacity = 16;

    input::InputEvent MakeButton(input::InputEventKind kind, input::ControlId control, int64_t timestamp_ns)
    {
        input::InputEvent event{};
        event.TimestampNs = timestamp_ns;
        event.Control = control;
        event.Kind = kind;
        event.Device = input::DeviceOf(control);

        return event;
    }

    input::InputEvent MakeAxis(input::InputEventKind kind, input::ControlId control, float x, float y)
    {
        input::InputEvent event{};
        event.Control = control;
        event.Kind = kind;
        event.Device = input::DeviceOf(control);
        event.X = x;
        event.Y = y;

        return event;
    }

    input::InputEvent MakeText(uint32_t code_point)
    {
        input::InputEvent event{};
        event.Kind = input::InputEventKind::Text;
        event.CodePoint = code_point;

        return event;
    }

    input::InputEvent MakeKind(input::InputEventKind kind, int64_t timestamp_ns)
    {
        input::InputEvent event{};
        event.TimestampNs = timestamp_ns;
        event.Kind = kind;

        return event;
    }
}

TEST(FrameAssembler, PressSetsDownAndTheEdgeFlag)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::KeyW, 100));

    const input::InputFrame& frame = assembler.FinishFrame();
    const input::ControlState& state = frame.State(input::ControlId::KeyW);

    EXPECT_TRUE(state.Down);
    EXPECT_TRUE(state.PressedThisFrame);
    EXPECT_FALSE(state.ReleasedThisFrame);
    EXPECT_EQ(state.LastChangeNs, 100);
    EXPECT_EQ(frame.Events().size(), 1u);
}

TEST(FrameAssembler, ReleaseClearsDownAndSetsTheEdgeFlag)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::KeyW, 100));
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonUp, input::ControlId::KeyW, 200));

    const input::ControlState& state = assembler.FinishFrame().State(input::ControlId::KeyW);

    EXPECT_FALSE(state.Down);
    EXPECT_FALSE(state.PressedThisFrame);
    EXPECT_TRUE(state.ReleasedThisFrame);
    EXPECT_EQ(state.LastChangeNs, 200);
}

TEST(FrameAssembler, PressAndReleaseInOneFrameKeepBothFlags)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::Space, 100));
    assembler.Append(MakeButton(input::InputEventKind::ButtonUp, input::ControlId::Space, 150));

    const input::ControlState& state = assembler.FinishFrame().State(input::ControlId::Space);

    EXPECT_FALSE(state.Down);
    EXPECT_TRUE(state.PressedThisFrame);
    EXPECT_TRUE(state.ReleasedThisFrame);
}

TEST(FrameAssembler, DownPersistsAcrossFramesWhileTheFlagsClear)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::KeyA, 100));
    assembler.StartFrame();

    const input::InputFrame& frame = assembler.FinishFrame();
    const input::ControlState& state = frame.State(input::ControlId::KeyA);

    EXPECT_TRUE(state.Down);
    EXPECT_FALSE(state.PressedThisFrame);
    EXPECT_EQ(state.LastChangeNs, 100);
    EXPECT_TRUE(frame.Events().empty());
}

TEST(FrameAssembler, KeyRepeatDoesNotPressAgain)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::KeyA, 100));
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::KeyA, 600));

    const input::ControlState& state = assembler.FinishFrame().State(input::ControlId::KeyA);

    EXPECT_TRUE(state.Down);
    EXPECT_FALSE(state.PressedThisFrame);
    EXPECT_EQ(state.LastChangeNs, 100);
}

TEST(FrameAssembler, DeltasSumInAFrameAndResetAtStartFrame)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::MouseX, 3.0f, 0.0f));
    assembler.Append(MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::MouseX, -1.0f, 0.0f));
    assembler.Append(MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::MouseX, 0.5f, 0.0f));

    EXPECT_FLOAT_EQ(assembler.FinishFrame().State(input::ControlId::MouseX).Delta, 2.5f);

    assembler.StartFrame();

    EXPECT_FLOAT_EQ(assembler.FinishFrame().State(input::ControlId::MouseX).Delta, 0.0f);
}

TEST(FrameAssembler, AxisValuePersistsAcrossFrames)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeAxis(input::InputEventKind::AxisValue, input::ControlId::GamepadLeftStickX, 0.75f, 0.0f));
    assembler.StartFrame();

    EXPECT_FLOAT_EQ(assembler.FinishFrame().State(input::ControlId::GamepadLeftStickX).Value, 0.75f);
}

TEST(FrameAssembler, CursorKeepsTheLastValueAcrossFrames)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeAxis(input::InputEventKind::CursorPosition, input::ControlId::Invalid, 10.0f, 20.0f));
    assembler.Append(MakeAxis(input::InputEventKind::CursorPosition, input::ControlId::Invalid, 30.0f, 40.0f));
    assembler.StartFrame();

    const input::InputFrame& frame = assembler.FinishFrame();

    EXPECT_FLOAT_EQ(frame.CursorX(), 30.0f);
    EXPECT_FLOAT_EQ(frame.CursorY(), 40.0f);
}

TEST(FrameAssembler, FocusLossReleasesEveryHeldControlWithSynthesizedEvents)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::KeyW, 100));
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::MouseLeft, 110));
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::GamepadSouth, 120));
    assembler.StartFrame();
    assembler.Append(MakeKind(input::InputEventKind::FocusLost, 500));

    const input::InputFrame& frame = assembler.FinishFrame();

    EXPECT_FALSE(frame.HasFocus());

    const std::array<input::ControlId, 3> heldControls
    {
        input::ControlId::KeyW,
        input::ControlId::MouseLeft,
        input::ControlId::GamepadSouth,
    };

    for (input::ControlId control : heldControls)
    {
        const input::ControlState& state = frame.State(control);

        EXPECT_FALSE(state.Down) << input::ToString(control);
        EXPECT_TRUE(state.ReleasedThisFrame) << input::ToString(control);
        EXPECT_EQ(state.LastChangeNs, 500) << input::ToString(control);
    }

    // the focus event, then three releases
    ASSERT_EQ(frame.Events().size(), 4u);

    for (size_t eventIndex = 1; eventIndex < frame.Events().size(); ++eventIndex)
    {
        const input::InputEvent& release = frame.Events()[eventIndex];

        EXPECT_EQ(release.Kind, input::InputEventKind::ButtonUp);
        EXPECT_TRUE(input::Any(release.Flags & input::EventFlags::Synthesized));
        EXPECT_EQ(release.TimestampNs, 500);
    }
}

TEST(FrameAssembler, FocusGainedSetsFocus)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeKind(input::InputEventKind::FocusLost, 10));
    assembler.Append(MakeKind(input::InputEventKind::FocusGained, 20));

    EXPECT_TRUE(assembler.FinishFrame().HasFocus());
}

TEST(FrameAssembler, AdjacentDeltasFoldWhenTheEventListIsFull)
{
    input::FrameAssembler assembler(4, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::KeyA, 1));
    assembler.Append(MakeButton(input::InputEventKind::ButtonUp, input::ControlId::KeyA, 2));
    assembler.Append(MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::MouseX, 1.0f, 0.0f));
    assembler.Append(MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::MouseX, 2.0f, 0.0f));

    input::InputEvent late = MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::MouseX, 4.0f, 0.0f);
    late.TimestampNs = 99;
    assembler.Append(late);

    const input::InputFrame& frame = assembler.FinishFrame();

    ASSERT_EQ(frame.Events().size(), 4u);
    EXPECT_FLOAT_EQ(frame.Events()[3].X, 6.0f);
    EXPECT_EQ(frame.Events()[3].TimestampNs, 99);
    EXPECT_EQ(frame.DroppedEventCount(), 0);
    EXPECT_FLOAT_EQ(frame.State(input::ControlId::MouseX).Delta, 7.0f);
}

TEST(FrameAssembler, ButtonDownWhenFullIsDroppedButStillUpdatesState)
{
    input::FrameAssembler assembler(4, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::MouseX, 1.0f, 0.0f));
    assembler.Append(MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::MouseY, 1.0f, 0.0f));
    assembler.Append(MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::WheelY, 1.0f, 0.0f));
    assembler.Append(MakeAxis(input::InputEventKind::AxisDelta, input::ControlId::WheelX, 1.0f, 0.0f));
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, input::ControlId::KeyF, 7));

    const input::InputFrame& frame = assembler.FinishFrame();
    const input::ControlState& state = frame.State(input::ControlId::KeyF);

    EXPECT_EQ(frame.Events().size(), 4u);
    EXPECT_EQ(frame.DroppedEventCount(), 1);
    EXPECT_TRUE(state.Down);
    EXPECT_TRUE(state.PressedThisFrame);
}

TEST(FrameAssembler, CursorFoldsIntoTheLastCursorEventWhenFull)
{
    input::FrameAssembler assembler(1, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeAxis(input::InputEventKind::CursorPosition, input::ControlId::Invalid, 1.0f, 2.0f));
    assembler.Append(MakeAxis(input::InputEventKind::CursorPosition, input::ControlId::Invalid, 5.0f, 6.0f));

    const input::InputFrame& frame = assembler.FinishFrame();

    ASSERT_EQ(frame.Events().size(), 1u);
    EXPECT_FLOAT_EQ(frame.Events()[0].X, 5.0f);
    EXPECT_FLOAT_EQ(frame.Events()[0].Y, 6.0f);
    EXPECT_EQ(frame.DroppedEventCount(), 0);
}

TEST(FrameAssembler, TextBeyondCapacityIsCountedAsDropped)
{
    input::FrameAssembler assembler(k_EventCapacity, 2);
    assembler.StartFrame();
    assembler.Append(MakeText(U'a'));
    assembler.Append(MakeText(U'b'));
    assembler.Append(MakeText(U'c'));
    assembler.Append(MakeText(0x1F600));

    const input::InputFrame& frame = assembler.FinishFrame();

    ASSERT_EQ(frame.Text().size(), 2u);
    EXPECT_EQ(frame.Text()[0], U'a');
    EXPECT_EQ(frame.Text()[1], U'b');
    EXPECT_EQ(frame.DroppedEventCount(), 2);
}

TEST(FrameAssembler, CaptureFollowsTheDeviceClassOfTheControl)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.SetCapture(input::CaptureFlags::Keyboard);

    const input::InputFrame& frame = assembler.FinishFrame();

    EXPECT_TRUE(frame.IsCaptured(input::ControlId::KeyA));
    EXPECT_FALSE(frame.IsCaptured(input::ControlId::MouseLeft));
    EXPECT_FALSE(frame.IsCaptured(input::ControlId::GamepadSouth));

    assembler.StartFrame();

    EXPECT_EQ(assembler.FinishFrame().Capture(), input::CaptureFlags::Invalid);
}

TEST(FrameAssembler, AControlOutsideTheSpaceIsDroppedAndCounted)
{
    input::FrameAssembler assembler(k_EventCapacity, k_TextCapacity);
    assembler.StartFrame();
    assembler.Append(MakeButton(input::InputEventKind::ButtonDown, static_cast<input::ControlId>(0x0300), 1));

    const input::InputFrame& frame = assembler.FinishFrame();

    EXPECT_TRUE(frame.Events().empty());
    EXPECT_EQ(frame.DroppedEventCount(), 1);
}
