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

    /** Returns the GPUToCPU buffer that the source buffer's first size_bytes were copied into, read back as words. */
    [[nodiscard]] std::vector<uint32_t> ReadBackWords(const GraphicsResource& source, size_t size_bytes);
    /** Creates a buffer and waits for it. Fails the test when the reply does not complete. */
    [[nodiscard]] GraphicsResource CreateBufferAndWait(const BufferCreateInfo& create_info, std::span<const RhiBufferResourceData> initial_data);
    void DestroyAndWait(const GraphicsResource& resource);

    static constexpr uint64_t k_TimeoutNs = 5'000'000'000ull;

    static std::unique_ptr<rhi::RhiSystem> rhiSystem;
    std::unique_ptr<ResourceContext> context;
};

[[nodiscard]] std::vector<uint32_t> MakeWordPattern(uint32_t count, uint32_t seed);

#endif // !RESOURCE_CONTEXT_TESTS_FIXTURE_HPP
