#pragma once
#ifndef RESOURCE_CONTEXT_TESTS_FIXTURE_HPP
#define RESOURCE_CONTEXT_TESTS_FIXTURE_HPP
#include "ResourceContext.hpp"
#include "RhiSystem.hpp"
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

/** One headless RhiSystem for the whole suite (with full validation), and a fresh ResourceContext for every test.
 *  The runner fails a test run that prints any validation message, so a test that passes here also proves that the
 *  context issued valid Vulkan. */
class ResourceContextTest : public ::testing::Test
{
public:
    static void SetUpTestSuite();
    static void TearDownTestSuite();

protected:
    void SetUp() override;
    void TearDown() override;

    /** Returns the first size_bytes of the source buffer, copied into a GPUToCPU buffer and read back as words. */
    [[nodiscard]] std::vector<uint32_t> ReadBackWords(const GraphicsResource& source, size_t size_bytes);
    /** Returns the first size_bytes of the image's mip 0, copied into a GPUToCPU buffer and read back as words. The
     *  usage is the image's usage before the copy and the usage it must have after. */
    [[nodiscard]] std::vector<uint32_t> ReadBackImageWords(const GraphicsResource& image, rhi::ImageUsageTransition usage, size_t size_bytes);
    /** Creates the GPUToCPU buffer that a readback copies into. */
    [[nodiscard]] GraphicsResource CreateReadbackBuffer(size_t size_bytes);
    /** Maps the readback buffer, copies out its words and destroys it. */
    [[nodiscard]] std::vector<uint32_t> ReadWordsAndDestroy(const GraphicsResource& readback, size_t size_bytes);
    /** Destroys the test's context, then makes a new one with a future table of `max_futures` slots. The old context
     *  goes first: two contexts would submit to the same transfer queue from two threads. */
    void RecreateContext(int32_t max_futures);
    /** Destroys the test's context, then makes a new one with `max_futures` future slots and a staging ring of
     *  `staging_ring_size` bytes. */
    void RecreateContextWithRing(int32_t max_futures, uint64_t staging_ring_size);
    /** Creates a buffer and waits for it. Fails the test when the future does not complete. */
    [[nodiscard]] GraphicsResource CreateBufferAndWait(const BufferCreateInfo& create_info, std::span<const RhiBufferResourceData> initial_data);
    void DestroyAndWait(const GraphicsResource& resource);

    static constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;

    static std::unique_ptr<rhi::RhiSystem> rhiSystem;
    std::unique_ptr<ResourceContext> context;
};

[[nodiscard]] std::vector<uint32_t> MakeWordPattern(uint32_t count, uint32_t seed);

#endif // !RESOURCE_CONTEXT_TESTS_FIXTURE_HPP
