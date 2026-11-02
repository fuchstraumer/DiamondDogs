#pragma once
#ifndef DIAMOND_DOGS_PLATFORM_SYSTEM_GLFW_INPUT_TRANSLATION_HPP
#define DIAMOND_DOGS_PLATFORM_SYSTEM_GLFW_INPUT_TRANSLATION_HPP
#include "InputTypes.hpp"

/**
 * @file GlfwInputTranslation.hpp
 * @brief Pure functions from GLFW token values to the control space of InputSystem. They need no window, and the
 * header includes no GLFW header, so a test can call them directly.
 */

/** @brief Returns the control for a GLFW key token, or ControlId::Invalid for GLFW_KEY_UNKNOWN and for a key that
 * the control space has no entry for. GLFW names its tokens after the US layout position, so the result is a
 * physical key. */
[[nodiscard]] input::ControlId ControlFromGlfwKey(int glfw_key) noexcept;

/** @brief Returns the control for a GLFW mouse button index, or ControlId::Invalid for an index above 4. */
[[nodiscard]] input::ControlId ControlFromGlfwMouseButton(int button) noexcept;

/** @brief Returns the modifier flags for a GLFW modifier bit mask. */
[[nodiscard]] input::ModifierFlags ModifiersFromGlfw(int mods) noexcept;

#endif // !DIAMOND_DOGS_PLATFORM_SYSTEM_GLFW_INPUT_TRANSLATION_HPP
