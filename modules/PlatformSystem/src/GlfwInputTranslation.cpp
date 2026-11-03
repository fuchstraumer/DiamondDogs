#include "GlfwInputTranslation.hpp"
#include "InputTypes.hpp"

#include "GLFW/glfw3.h"

input::ControlId ControlFromGlfwKey(int glfw_key) noexcept
{
    switch (glfw_key)
    {
    case GLFW_KEY_A: return input::ControlId::KeyA;
    case GLFW_KEY_B: return input::ControlId::KeyB;
    case GLFW_KEY_C: return input::ControlId::KeyC;
    case GLFW_KEY_D: return input::ControlId::KeyD;
    case GLFW_KEY_E: return input::ControlId::KeyE;
    case GLFW_KEY_F: return input::ControlId::KeyF;
    case GLFW_KEY_G: return input::ControlId::KeyG;
    case GLFW_KEY_H: return input::ControlId::KeyH;
    case GLFW_KEY_I: return input::ControlId::KeyI;
    case GLFW_KEY_J: return input::ControlId::KeyJ;
    case GLFW_KEY_K: return input::ControlId::KeyK;
    case GLFW_KEY_L: return input::ControlId::KeyL;
    case GLFW_KEY_M: return input::ControlId::KeyM;
    case GLFW_KEY_N: return input::ControlId::KeyN;
    case GLFW_KEY_O: return input::ControlId::KeyO;
    case GLFW_KEY_P: return input::ControlId::KeyP;
    case GLFW_KEY_Q: return input::ControlId::KeyQ;
    case GLFW_KEY_R: return input::ControlId::KeyR;
    case GLFW_KEY_S: return input::ControlId::KeyS;
    case GLFW_KEY_T: return input::ControlId::KeyT;
    case GLFW_KEY_U: return input::ControlId::KeyU;
    case GLFW_KEY_V: return input::ControlId::KeyV;
    case GLFW_KEY_W: return input::ControlId::KeyW;
    case GLFW_KEY_X: return input::ControlId::KeyX;
    case GLFW_KEY_Y: return input::ControlId::KeyY;
    case GLFW_KEY_Z: return input::ControlId::KeyZ;
    case GLFW_KEY_1: return input::ControlId::Digit1;
    case GLFW_KEY_2: return input::ControlId::Digit2;
    case GLFW_KEY_3: return input::ControlId::Digit3;
    case GLFW_KEY_4: return input::ControlId::Digit4;
    case GLFW_KEY_5: return input::ControlId::Digit5;
    case GLFW_KEY_6: return input::ControlId::Digit6;
    case GLFW_KEY_7: return input::ControlId::Digit7;
    case GLFW_KEY_8: return input::ControlId::Digit8;
    case GLFW_KEY_9: return input::ControlId::Digit9;
    case GLFW_KEY_0: return input::ControlId::Digit0;
    case GLFW_KEY_ENTER: return input::ControlId::Enter;
    case GLFW_KEY_ESCAPE: return input::ControlId::Escape;
    case GLFW_KEY_BACKSPACE: return input::ControlId::Backspace;
    case GLFW_KEY_TAB: return input::ControlId::Tab;
    case GLFW_KEY_SPACE: return input::ControlId::Space;
    case GLFW_KEY_MINUS: return input::ControlId::Minus;
    case GLFW_KEY_EQUAL: return input::ControlId::Equal;
    case GLFW_KEY_LEFT_BRACKET: return input::ControlId::BracketLeft;
    case GLFW_KEY_RIGHT_BRACKET: return input::ControlId::BracketRight;
    case GLFW_KEY_BACKSLASH: return input::ControlId::Backslash;
    case GLFW_KEY_SEMICOLON: return input::ControlId::Semicolon;
    case GLFW_KEY_APOSTROPHE: return input::ControlId::Quote;
    case GLFW_KEY_GRAVE_ACCENT: return input::ControlId::Backquote;
    case GLFW_KEY_COMMA: return input::ControlId::Comma;
    case GLFW_KEY_PERIOD: return input::ControlId::Period;
    case GLFW_KEY_SLASH: return input::ControlId::Slash;
    case GLFW_KEY_CAPS_LOCK: return input::ControlId::CapsLock;
    case GLFW_KEY_F1: return input::ControlId::F1;
    case GLFW_KEY_F2: return input::ControlId::F2;
    case GLFW_KEY_F3: return input::ControlId::F3;
    case GLFW_KEY_F4: return input::ControlId::F4;
    case GLFW_KEY_F5: return input::ControlId::F5;
    case GLFW_KEY_F6: return input::ControlId::F6;
    case GLFW_KEY_F7: return input::ControlId::F7;
    case GLFW_KEY_F8: return input::ControlId::F8;
    case GLFW_KEY_F9: return input::ControlId::F9;
    case GLFW_KEY_F10: return input::ControlId::F10;
    case GLFW_KEY_F11: return input::ControlId::F11;
    case GLFW_KEY_F12: return input::ControlId::F12;
    case GLFW_KEY_PRINT_SCREEN: return input::ControlId::PrintScreen;
    case GLFW_KEY_SCROLL_LOCK: return input::ControlId::ScrollLock;
    case GLFW_KEY_PAUSE: return input::ControlId::Pause;
    case GLFW_KEY_INSERT: return input::ControlId::Insert;
    case GLFW_KEY_HOME: return input::ControlId::Home;
    case GLFW_KEY_PAGE_UP: return input::ControlId::PageUp;
    case GLFW_KEY_DELETE: return input::ControlId::Delete;
    case GLFW_KEY_END: return input::ControlId::End;
    case GLFW_KEY_PAGE_DOWN: return input::ControlId::PageDown;
    case GLFW_KEY_RIGHT: return input::ControlId::ArrowRight;
    case GLFW_KEY_LEFT: return input::ControlId::ArrowLeft;
    case GLFW_KEY_DOWN: return input::ControlId::ArrowDown;
    case GLFW_KEY_UP: return input::ControlId::ArrowUp;
    case GLFW_KEY_NUM_LOCK: return input::ControlId::NumLock;
    case GLFW_KEY_KP_DIVIDE: return input::ControlId::NumpadDivide;
    case GLFW_KEY_KP_MULTIPLY: return input::ControlId::NumpadMultiply;
    case GLFW_KEY_KP_SUBTRACT: return input::ControlId::NumpadSubtract;
    case GLFW_KEY_KP_ADD: return input::ControlId::NumpadAdd;
    case GLFW_KEY_KP_ENTER: return input::ControlId::NumpadEnter;
    case GLFW_KEY_KP_1: return input::ControlId::Numpad1;
    case GLFW_KEY_KP_2: return input::ControlId::Numpad2;
    case GLFW_KEY_KP_3: return input::ControlId::Numpad3;
    case GLFW_KEY_KP_4: return input::ControlId::Numpad4;
    case GLFW_KEY_KP_5: return input::ControlId::Numpad5;
    case GLFW_KEY_KP_6: return input::ControlId::Numpad6;
    case GLFW_KEY_KP_7: return input::ControlId::Numpad7;
    case GLFW_KEY_KP_8: return input::ControlId::Numpad8;
    case GLFW_KEY_KP_9: return input::ControlId::Numpad9;
    case GLFW_KEY_KP_0: return input::ControlId::Numpad0;
    case GLFW_KEY_KP_DECIMAL: return input::ControlId::NumpadDecimal;
    case GLFW_KEY_KP_EQUAL: return input::ControlId::NumpadEqual;
    case GLFW_KEY_MENU: return input::ControlId::ContextMenu;
    case GLFW_KEY_F13: return input::ControlId::F13;
    case GLFW_KEY_F14: return input::ControlId::F14;
    case GLFW_KEY_F15: return input::ControlId::F15;
    case GLFW_KEY_F16: return input::ControlId::F16;
    case GLFW_KEY_F17: return input::ControlId::F17;
    case GLFW_KEY_F18: return input::ControlId::F18;
    case GLFW_KEY_F19: return input::ControlId::F19;
    case GLFW_KEY_F20: return input::ControlId::F20;
    case GLFW_KEY_F21: return input::ControlId::F21;
    case GLFW_KEY_F22: return input::ControlId::F22;
    case GLFW_KEY_F23: return input::ControlId::F23;
    case GLFW_KEY_F24: return input::ControlId::F24;
    case GLFW_KEY_LEFT_CONTROL: return input::ControlId::ControlLeft;
    case GLFW_KEY_LEFT_SHIFT: return input::ControlId::ShiftLeft;
    case GLFW_KEY_LEFT_ALT: return input::ControlId::AltLeft;
    case GLFW_KEY_LEFT_SUPER: return input::ControlId::MetaLeft;
    case GLFW_KEY_RIGHT_CONTROL: return input::ControlId::ControlRight;
    case GLFW_KEY_RIGHT_SHIFT: return input::ControlId::ShiftRight;
    case GLFW_KEY_RIGHT_ALT: return input::ControlId::AltRight;
    case GLFW_KEY_RIGHT_SUPER: return input::ControlId::MetaRight;
    // The ISO key between left shift and Z. GLFW reports it as WORLD_2 on Win32 (scancode 0x56) and Wayland, and
    // as WORLD_1 on X11 and macOS, so both tokens are the same physical key.
    case GLFW_KEY_WORLD_1: return input::ControlId::IntlBackslash;
    case GLFW_KEY_WORLD_2: return input::ControlId::IntlBackslash;
    default: return input::ControlId::Invalid;
    }
}

input::ControlId ControlFromGlfwMouseButton(int button) noexcept
{
    switch (button)
    {
    case GLFW_MOUSE_BUTTON_LEFT: return input::ControlId::MouseLeft;
    case GLFW_MOUSE_BUTTON_RIGHT: return input::ControlId::MouseRight;
    case GLFW_MOUSE_BUTTON_MIDDLE: return input::ControlId::MouseMiddle;
    case GLFW_MOUSE_BUTTON_4: return input::ControlId::MouseBack;
    case GLFW_MOUSE_BUTTON_5: return input::ControlId::MouseForward;
    default: return input::ControlId::Invalid;
    }
}

input::ModifierFlags ModifiersFromGlfw(int mods) noexcept
{
    input::ModifierFlags flags = input::ModifierFlags::Invalid;

    if ((mods & GLFW_MOD_SHIFT) != 0)
    {
        flags |= input::ModifierFlags::Shift;
    }

    if ((mods & GLFW_MOD_CONTROL) != 0)
    {
        flags |= input::ModifierFlags::Control;
    }

    if ((mods & GLFW_MOD_ALT) != 0)
    {
        flags |= input::ModifierFlags::Alt;
    }

    if ((mods & GLFW_MOD_SUPER) != 0)
    {
        flags |= input::ModifierFlags::Super;
    }

    if ((mods & GLFW_MOD_CAPS_LOCK) != 0)
    {
        flags |= input::ModifierFlags::CapsLock;
    }

    if ((mods & GLFW_MOD_NUM_LOCK) != 0)
    {
        flags |= input::ModifierFlags::NumLock;
    }

    return flags;
}
