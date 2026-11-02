# InputSystem

InputSystem is the platform-free core of engine input. It defines one control space, one raw event record, the
`FrameAssembler` that turns events into an `InputFrame`, and a text log for record and replay.

**Status.** No platform source exists yet: nothing translates GLFW or Win32 input into `InputEvent`. Nothing in the
engine uses this module yet. The module depends on `foundation` through `ADD_MODULE` only, and includes no GLFW, Win32
or Vulkan header. The tests, `input_system_tests`, run with no window and no GPU.

## The control space

`input::ControlId` is one integer space for every control, so that a binding does not care which device a control
is on.

| Range | Controls |
|---|---|
| `0x0000` | `Invalid` |
| `0x0004` to `0x00E7` | Keyboard keys. The values are USB HID usage page 0x07 usage IDs. |
| `0x0100` to `0x0108` | Mouse buttons, `MouseX`, `MouseY`, `WheelX`, `WheelY` |
| `0x0200` to `0x0214` | Gamepad buttons and axes on the standard (Xbox) layout |

`k_ControlCount` is `0x0300`. It is the size of the state table in `FrameAssembler`.

**Why physical HID keys.** A binding wants a key position: WASD stays in the same place on an AZERTY keyboard.
Win32 scan codes, GLFW key tokens and DOM `KeyboardEvent.code` all map to HID usages with fixed tables, so each
platform source converts once. The enumerator names are the `KeyboardEvent.code` names (`KeyA`, `Digit1`,
`BracketLeft`, `NumpadAdd`, `MetaLeft`). Text is a separate stream of code points, because text needs the layout and
the IME.

`ToString` and `ParseControlId` use two sorted tables, one by name and one by value, in `src/ControlNameTable.hpp`.
The enum is sparse and wide, so a reflection library does not fit. The tests check every name round trip, and a
`static_assert` checks the sort order.

## The event record

`InputEvent` is a 32 byte trivially copyable record.

| Field | Meaning |
|---|---|
| `TimestampNs` | Nanoseconds on one steady clock. Each source converts once. |
| `X`, `Y` | The payload. See the kinds below. |
| `CodePoint` | A Unicode code point, for `Text`. |
| `Control` | The `ControlId`. `Invalid` for cursor, text and focus events. |
| `Kind` | The `InputEventKind`. |
| `Device`, `DeviceIndex` | The device class, and the slot (keyboard 0, mouse 0, gamepad N). |
| `Modifiers` | `ModifierFlags` that were held when the event happened. |
| `Flags` | `EventFlags`. `Synthesized` marks an event that the assembler made. |

Kinds:

- `ButtonDown` and `ButtonUp`: a button or key edge.
- `AxisValue`: an absolute value in `X`, for sticks and triggers.
- `AxisDelta`: a relative change in `X`, for `MouseX`, `MouseY`, `WheelX` and `WheelY`. Wheel units are notches,
  positive away from the user.
- `CursorPosition`: `X` and `Y` in framebuffer pixels from the top left.
- `Text`: a code point.
- `FocusGained` and `FocusLost`.

Every enum reserves 0 for `Invalid`.

## FrameAssembler

A platform source calls `StartFrame`, then `Append` for each event, then `SetCapture`, then `FinishFrame`. The
`InputFrame` that `FinishFrame` returns is valid until the next `StartFrame`. The assembler reserves its event list
and its text list once, in the constructor, and never grows them. The state table is a fixed `std::array`.

Rules:

- **Arrival order.** `Append` updates the state at once, in the order of the calls. The stored event list is for
  consumers that need the order. The state does not depend on it.
- **Edges.** `ButtonDown` on a control that is down (key repeat) changes nothing, so `PressedThisFrame` and
  `LastChangeNs` keep the values of the first press. A press and a release in one frame leave both flags true and
  `Down` false. `ButtonUp` on a control that is up changes nothing.
- **Persistence.** `StartFrame` clears the events, the text, the edge flags, every `Delta`, both dropped counts and the
  capture flags. `Down`, `Value`, `LastChangeNs`, the cursor and the focus state persist. Focus starts as gained.
- **Axes.** `AxisValue` sets `Value`. `AxisDelta` adds `X` to `Delta`. `LastChangeNs` is for buttons only.
- **Full event list.** If an `AxisDelta` or a `CursorPosition` arrives when the list is full, and the last stored
  event has the same kind and control, the new event folds into it: deltas sum, a cursor overwrites, and the newer
  timestamp wins. Any other event is dropped and counted in `DroppedEventCount`. The state is already correct in
  both cases, so a dropped button edge is still seen as `Down` and `PressedThisFrame`.
- **Full text list.** A code point that does not fit is dropped and counted in `DroppedTextCount`. The Text event
  itself still goes through the event list, so a full event list can also count it in `DroppedEventCount`.
- **Focus loss.** `FocusLost` sets the focus state to lost. Then, for every control that is down, the assembler
  appends a `ButtonUp` with the `Synthesized` flag and the same timestamp, through the same `Append` path. No key
  sticks.
- **Control outside the space.** A button or axis event whose control is `Invalid` or not below `k_ControlCount` is
  dropped and counted in `DroppedEventCount`.
- **Capture.** `SetCapture` stores which device classes the UI took this frame. `IsCaptured` tests the device class
  of a control. The raw state is not changed, and the consumer decides what to do.

Threading: the assembler is single-threaded and has no lock. A source that runs on another thread needs its own
hand-off, which is not part of this module yet.

## Input log

`WriteInputLog` returns JSON Lines text, one record per event, in a fixed key order:

```
{"t":123456,"kind":"ButtonDown","control":"KeyW","device":"Keyboard","index":0,"mods":0,"flags":0,"x":0,"y":0,"cp":0}
```

`mods` and `flags` are the raw bit values. A control that has no name (a value inside the control space that the
name table does not list) is written as a bare integer, for example `"control":50`. Control 0 is written as
`"Invalid"`. Floats use `std::to_chars` in the shortest form, so they round trip
exactly. The function builds the whole string, and the caller writes it in one operation.

`ReadInputLog` parses the text with `std::string_view::find` and `std::from_chars`. It reserves the event vector once, from the count of newline characters. It skips blank lines. The `control` field
can be a quoted name or an unquoted integer from 0 to `k_ControlCount - 1`. It checks
each record where it enters and returns the first error as an `InputLogError`: `MalformedLine`, `MissingKey`,
`UnknownControl`, `UnknownKind`, `UnknownDevice` or `MalformedNumber` (also a number that does not fit its field,
such as `index` above 255 or `cp` above 0x10FFFF). The error does not carry a line number yet.

The log is meant to let a test feed a recorded session through the assembler with no window, and to turn a bug
report from another machine into a file.
