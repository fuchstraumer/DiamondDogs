#include "TargetExtents.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace loom
{

namespace
{

    Extent2D RenderOf(const TargetExtents& state)
    {
        if (state.Target.IsEmpty() || state.Allocation.IsEmpty())
        {
            return Extent2D{};
        }

        const Extent2D scaled = ScaledExtent(state.Target, state.RenderScale);

        return Extent2D
        {
            .Width = std::min(scaled.Width, state.Allocation.Width),
            .Height = std::min(scaled.Height, state.Allocation.Height),
        };
    }

    // The render extent follows the other two; the change reported is the larger one
    ExtentChange UpdateRender(TargetExtents& state, ExtentChange change)
    {
        const Extent2D render = RenderOf(state);

        if (render == state.Render)
        {
            return change;
        }

        state.Render = render;
        return change == ExtentChange::Reallocate ? change : ExtentChange::Render;
    }

}

    ExtentChange UpdateTargetExtents(TargetExtents& state,
                                     Extent2D reported,
                                     double delta_seconds,
                                     double settle_seconds,
                                     bool resize_finished)
    {
        if (state.Allocation.IsEmpty() && !reported.IsEmpty())
        {
            state.Target = reported;
            state.Allocation = reported;
            state.StableSeconds = 0.0;
            return UpdateRender(state, ExtentChange::Reallocate);
        }

        if (reported != state.Target)
        {
            state.Target = reported;
            state.StableSeconds = 0.0;
        }
        else
        {
            state.StableSeconds += delta_seconds;
        }

        const bool differs = !state.Target.IsEmpty() && state.Target != state.Allocation;
        const bool settled = state.StableSeconds >= settle_seconds || resize_finished;

        if (differs && settled)
        {
            state.Allocation = state.Target;
            return UpdateRender(state, ExtentChange::Reallocate);
        }

        return UpdateRender(state, ExtentChange::None);
    }

    bool SetRenderScale(TargetExtents& state, float scale)
    {
        if (scale <= 0.0f || scale > 1.0f)
        {
            return false;
        }

        state.RenderScale = scale;
        state.Render = RenderOf(state);
        return true;
    }

    Extent2D ScaledExtent(Extent2D extent, float scale)
    {
        const auto scaleAxis = [scale](int32_t value)
        {
            const double scaled = std::round(static_cast<double>(value) * static_cast<double>(scale));
            return std::max(1, static_cast<int32_t>(scaled));
        };

        return Extent2D{ .Width = scaleAxis(extent.Width), .Height = scaleAxis(extent.Height) };
    }

}
