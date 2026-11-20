#pragma once
#ifndef DIAMOND_DOGS_PLANET_TIMELAPSE_AGENT_TOOLS_HPP
#define DIAMOND_DOGS_PLANET_TIMELAPSE_AGENT_TOOLS_HPP
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace planet_tools
{
    struct SimState
    {
        float TimeStepMy{ 0.5f };
        int32_t RenderMode{ 0 };
        float Time{ 0.0f };
        int32_t Step{ 0 };
        int32_t Width{ 1024 };
        int32_t Height{ 512 };
    };

    struct CaptureOutput
    {
        bool Succeeded{ false };
        std::string Error;
        std::string Path;
        int32_t Width{ 0 };
        int32_t Height{ 0 };
    };

    struct TimelapseToolHost
    {
        SimState& Sim;
        std::function<void(std::string_view field_path)> OnSimChanged;
        std::function<std::optional<std::string>(int32_t steps, float dt)> Step;
        std::function<std::optional<std::string>()> Reset;
        std::function<CaptureOutput(int32_t x,
                                      int32_t y,
                                      int32_t width,
                                      int32_t height,
                                      uint32_t downsample,
                                      int32_t mode,
                                      std::string_view name)> CaptureArea;
    };

    [[nodiscard]] int RunTimelapseToolServer(int32_t port, TimelapseToolHost& host);
}

#endif // !DIAMOND_DOGS_PLANET_TIMELAPSE_AGENT_TOOLS_HPP
