#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_BOOT_TIMINGS_HPP
#define DIAMOND_DOGS_BOOTSTRAP_BOOT_TIMINGS_HPP
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

/** CPU time spans for each boot stage, module start, bundle open and device step. bootstrap sets
 *  BOOTSTRAP_TIMINGS_ENABLED to 1 in Debug and RelWithDebInfo, and to 0 in Release and MinSizeRel. With 0 the same
 *  classes exist with empty inline bodies, so we don't need macros at call sites to guard timing code. */
#ifndef BOOTSTRAP_TIMINGS_ENABLED
#define BOOTSTRAP_TIMINGS_ENABLED 0
#endif

namespace bootstrap
{
    enum class Stage : uint8_t;

    enum class BootSpanKind : uint8_t
    {
        Invalid = 0,
        Stage,      // A high-level boot stage, like Core, Content, Window, Device or Gpu
        Module,     // The start of a module, identified by its descriptor name
        Bundle,     // The opening of a bundle (content data), identified by its file name
        Step,       // A fine-grained individual operation within a stage, module or bundle, for detailed timing analysis
    };

    struct BootSpan
    {
        Stage OwningStage{};
        BootSpanKind Kind{ BootSpanKind::Invalid };
        // a descriptor name, a bundle file name or a literal: every name outlives the boot
        std::string_view Name;
        int64_t StartNanoseconds{ 0 };
        // -1 until the span ends
        int64_t Nanoseconds{ -1 };
    };

    inline constexpr int32_t k_MaxBootSpans = 64;

#if BOOTSTRAP_TIMINGS_ENABLED
    /** @brief Fixed array of timing spans, in order that they're created/begun. There is no synchronization as we
      * run bootstrap on a single thread (currently). */
    class BootTimings
    {
    public:
        BootTimings() noexcept;

        /** @brief Starts a span, and returns its index. Returns -1 and counts a dropped span when the array is
         *  full. */
        [[nodiscard]] int32_t Begin(Stage stage, BootSpanKind kind, std::string_view name) noexcept;
        /** @brief Ends the span that Begin returned. Does nothing for -1. */
        void End(int32_t span_index) noexcept;
        void Clear() noexcept;

        [[nodiscard]] std::span<const BootSpan> Spans() const noexcept;
        [[nodiscard]] int32_t DroppedSpans() const noexcept;

    private:
        std::array<BootSpan, k_MaxBootSpans> spans;
        int32_t spanCount{ 0 };
        int32_t droppedSpans{ 0 };
    };

    /** @brief A helper RAII class that starts a boot span on construction and ends it on destruction: declare
      * in a scoped block to time that specific block */
    class ScopedBootSpan
    {
    public:
        ScopedBootSpan(BootTimings& _timings, Stage stage, BootSpanKind kind, std::string_view name) noexcept;
        ~ScopedBootSpan();
        ScopedBootSpan(const ScopedBootSpan&) = delete;
        ScopedBootSpan& operator=(const ScopedBootSpan&) = delete;

    private:
        BootTimings& timings;
        int32_t spanIndex{ -1 };
    };
#else
    class BootTimings
    {
    public:
        [[nodiscard]] int32_t Begin(Stage, BootSpanKind, std::string_view) noexcept
        {
            return -1;
        }

        void End(int32_t) noexcept
        {
        }

        void Clear() noexcept
        {
        }

        [[nodiscard]] std::span<const BootSpan> Spans() const noexcept
        {
            return {};
        }

        [[nodiscard]] int32_t DroppedSpans() const noexcept
        {
            return 0;
        }
    };

    class ScopedBootSpan
    {
    public:
        ScopedBootSpan(BootTimings&, Stage, BootSpanKind, std::string_view) noexcept
        {
        }

        ScopedBootSpan(const ScopedBootSpan&) = delete;
        ScopedBootSpan& operator=(const ScopedBootSpan&) = delete;
    };
#endif

    /** @brief Returns the spans as a table: one row per stage, with its modules, bundles and steps under it, then
     *  the total of the stages. `header_detail` goes in the header line beside the build configuration, for example
     *  the validation level, because validation inflates instance and device creation. In a build without timings,
     *  returns one line that says so. */
    [[nodiscard]] std::string FormatBootTimings(const BootTimings& timings, std::string_view header_detail);
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_BOOT_TIMINGS_HPP
