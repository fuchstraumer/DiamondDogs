#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "PlatformSystem.hpp"
#include "PlatformTypes.hpp"

constexpr static PlatformWindowCreateInfo s_DefaultCreateInfo
{
    "UnitTestWindow", // window name
    nullptr, // use primary display
    PlatformWindowMode::Windowed,
    800, // initial width
    600, // initial height
    0,   // initial pos x
    0,   // initial pos y
    PlatformWindowBehaviorFlags
    {
        true,   // Resizable
        true,   // Moveable
        true,   // Decorated
        false,  // FocusOnShow
        false   // CenterMouse
    }   // default behavior flags
};

class PlatformSystemTest : public ::testing::Test
{
};
