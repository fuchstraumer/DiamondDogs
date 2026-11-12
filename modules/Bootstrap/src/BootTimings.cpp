#include "BootTimings.hpp"
#include "BootResult.hpp"
#include <chrono>
#include <format>
#include <iterator>

#ifndef BOOTSTRAP_BUILD_CONFIG
#define BOOTSTRAP_BUILD_CONFIG "unknown configuration"
#endif

#if BOOTSTRAP_TIMINGS_ENABLED
namespace
{
    [[nodiscard]] int64_t NowNanoseconds() noexcept;
    [[nodiscard]] std::string_view KindLabel(bootstrap::BootSpanKind kind) noexcept;
    void AppendSpanRow(std::string& output, const bootstrap::BootSpan& span);
}
#endif

namespace bootstrap
{
#if BOOTSTRAP_TIMINGS_ENABLED
    BootTimings::BootTimings() noexcept = default;

    int32_t BootTimings::Begin(Stage stage, BootSpanKind kind, std::string_view name) noexcept
    {
        if (spanCount == k_MaxBootSpans)
        {
            ++droppedSpans;
            return -1;
        }

        const int32_t spanIndex = spanCount;

        spans[static_cast<size_t>(spanIndex)] = BootSpan
        {
            .OwningStage = stage,
            .Kind = kind,
            .Name = name,
            .StartNanoseconds = NowNanoseconds(),
        };

        ++spanCount;
        return spanIndex;
    }

    void BootTimings::End(int32_t span_index) noexcept
    {
        if (span_index == -1)
        {
            return;
        }

        BootSpan& span = spans[static_cast<size_t>(span_index)];

        span.Nanoseconds = NowNanoseconds() - span.StartNanoseconds;
    }

    void BootTimings::Clear() noexcept
    {
        spanCount = 0;
        droppedSpans = 0;
    }

    std::span<const BootSpan> BootTimings::Spans() const noexcept
    {
        return std::span<const BootSpan>{ spans.data(), static_cast<size_t>(spanCount) };
    }

    int32_t BootTimings::DroppedSpans() const noexcept
    {
        return droppedSpans;
    }

    ScopedBootSpan::ScopedBootSpan(BootTimings& _timings,
                                   Stage stage,
                                   BootSpanKind kind,
                                   std::string_view name) noexcept :
        timings{ _timings },
        spanIndex{ _timings.Begin(stage, kind, name) }
    {
    }

    ScopedBootSpan::~ScopedBootSpan()
    {
        timings.End(spanIndex);
    }

    std::string FormatBootTimings(const BootTimings& timings, std::string_view header_detail)
    {
        std::string output = std::format("boot timings ({}, {})\n  {:<9} {:<26} {:>9}",
                                         BOOTSTRAP_BUILD_CONFIG,
                                         header_detail,
                                         "stage",
                                         "step",
                                         "ms");
        int64_t totalNanoseconds = 0;

        for (const BootSpan& span : timings.Spans())
        {
            AppendSpanRow(output, span);

            if (span.Kind == BootSpanKind::Stage && span.Nanoseconds >= 0)
            {
                totalNanoseconds += span.Nanoseconds;
            }
        }

        const double totalMilliseconds = static_cast<double>(totalNanoseconds) / 1.0e6;

        std::format_to(std::back_inserter(output), "\n  {:<36} {:>9.1f}", "total", totalMilliseconds);

        if (timings.DroppedSpans() > 0)
        {
            std::format_to(std::back_inserter(output), "\n  ({} spans dropped: the array is full)", timings.DroppedSpans());
        }

        return output;
    }
#else
    std::string FormatBootTimings(const BootTimings&, std::string_view)
    {
        return std::format("boot timings are compiled out of this build ({})", BOOTSTRAP_BUILD_CONFIG);
    }
#endif
}

#if BOOTSTRAP_TIMINGS_ENABLED
namespace
{
    int64_t NowNanoseconds() noexcept
    {
        const auto sinceEpoch = std::chrono::steady_clock::now().time_since_epoch();

        return std::chrono::duration_cast<std::chrono::nanoseconds>(sinceEpoch).count();
    }

    std::string_view KindLabel(bootstrap::BootSpanKind kind) noexcept
    {
        switch (kind)
        {
        case bootstrap::BootSpanKind::Invalid: return "invalid";
        case bootstrap::BootSpanKind::Stage: return "stage";
        case bootstrap::BootSpanKind::Module: return "module";
        case bootstrap::BootSpanKind::Bundle: return "bundle";
        case bootstrap::BootSpanKind::Step: return "step";
        }

        return "invalid";
    }

    void AppendSpanRow(std::string& output, const bootstrap::BootSpan& span)
    {
        const std::string milliseconds = span.Nanoseconds >= 0
                                             ? std::format("{:.1f}", static_cast<double>(span.Nanoseconds) / 1.0e6)
                                             : std::string{ "-" };

        if (span.Kind == bootstrap::BootSpanKind::Stage)
        {
            std::format_to(std::back_inserter(output), "\n  {:<36} {:>9}", span.Name, milliseconds);
            return;
        }

        std::format_to(std::back_inserter(output),
                       "\n    {:<7} {:<26} {:>9}",
                       KindLabel(span.Kind),
                       span.Name,
                       milliseconds);
    }
}
#endif
