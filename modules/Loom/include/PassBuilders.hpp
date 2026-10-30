#pragma once
#ifndef DIAMOND_DOGS_LOOM_PASS_BUILDERS_HPP
#define DIAMOND_DOGS_LOOM_PASS_BUILDERS_HPP
#include "Declarations.hpp"
#include "LoomKeys.hpp"
#include <cstdint>
#include <memory>
#include <source_location>
#include <string_view>

namespace loom
{

    struct PassRecord;
    class GraphBuilder;
    class SequenceBuilder;

    // The builder an AddDispatch lambda receives. Every call records; none checks or fails (Freeze judges). Only
    // GraphBuilder and SequenceBuilder can make one, so a builder cannot exist outside an Add* call.
    class DispatchBuilder
    {
    public:
        ~DispatchBuilder();

        DispatchBuilder(const DispatchBuilder&) = delete;
        DispatchBuilder& operator=(const DispatchBuilder&) = delete;
        DispatchBuilder(DispatchBuilder&&) = delete;
        DispatchBuilder& operator=(DispatchBuilder&&) = delete;

        void Shader(std::string_view module_name, std::string_view entry_point,
                    std::source_location where = std::source_location::current());

        // A pass pin refines a graph pin on the same axis (S9, G6).
        void Pin(std::string_view axis_name, AxisValue axis_value,
                 std::source_location where = std::source_location::current());

        void After(AnchorKey anchor, std::source_location where = std::source_location::current());
        void Before(AnchorKey anchor, std::source_location where = std::source_location::current());

        void Bind(std::string_view binding_name, ResourceKey<Buffer> resource,
                  std::source_location where = std::source_location::current());
        void Bind(std::string_view binding_name, ResourceKey<Buffer> resource, Narrow narrow,
                  std::source_location where = std::source_location::current());
        void Bind(std::string_view binding_name, ResourceKey<Image> resource,
                  std::source_location where = std::source_location::current());
        void Bind(std::string_view binding_name, ResourceKey<Image> resource, Narrow narrow,
                  std::source_location where = std::source_location::current());
        void Bind(std::string_view binding_name, ResourceKey<Sampler> resource,
                  std::source_location where = std::source_location::current());

        // Reads the history slot: last frame's contents (R4).
        void BindPrevious(std::string_view binding_name, ResourceKey<Buffer> resource,
                          std::source_location where = std::source_location::current());
        void BindPrevious(std::string_view binding_name, ResourceKey<Image> resource,
                          std::source_location where = std::source_location::current());

        // The entry point constants (G8). T's size and alignment go into the record for the LM305 and LM704 checks.
        template<typename T>
        void Params(ParamsKey<T> params, std::source_location where = std::source_location::current())
        {
            addParams(params.ToRef(), static_cast<uint32_t>(sizeof(T)), static_cast<uint32_t>(alignof(T)), where);
        }

        void DispatchThreadsOver(ResourceKey<Image> resource,
                                 std::source_location where = std::source_location::current());
        void DispatchThreads(int32_t x, int32_t y, int32_t z,
                             std::source_location where = std::source_location::current());
        void DispatchIndirect(ResourceKey<Buffer> arguments,
                              std::source_location where = std::source_location::current());

    private:
        friend class GraphBuilder;
        friend class SequenceBuilder;

        DispatchBuilder(PassKey pass, RunPolicy run_policy, QueueAffinity queue_affinity, std::source_location where);

        void addParams(KeyRef params, uint32_t byte_size, uint32_t alignment, std::source_location where);

        // Hands the finished record to the committer. The builder is empty afterwards.
        [[nodiscard]] std::unique_ptr<PassRecord> takeRecord() noexcept;

        std::unique_ptr<PassRecord> record;
    };

}

#endif // !DIAMOND_DOGS_LOOM_PASS_BUILDERS_HPP
