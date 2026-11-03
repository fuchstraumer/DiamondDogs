#pragma once
#ifndef DIAMOND_DOGS_LOOM_TESTS_FREEZE_TEST_HELPERS_HPP
#define DIAMOND_DOGS_LOOM_TESTS_FREEZE_TEST_HELPERS_HPP
#include "Declarations.hpp"
#include "ImageDataFormats.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomKeys.hpp"
#include <gtest/gtest.h>

// The keys and declarations that more than one Freeze test file uses. Each file adds its own keys beside these.
namespace freeze_tests
{

    // One name published as a buffer, and the same name used as an image: the LM107 tests need both.
    inline constexpr loom::ResourceKey<loom::Buffer> k_Data{ "Test.Data" };
    inline constexpr loom::ResourceKey<loom::Image> k_DataAsImage{ "Test.Data" };
    inline constexpr loom::ResourceKey<loom::Image> k_Albedo{ "Test.Albedo" };
    inline constexpr loom::FrameTargetKey k_Backbuffer{ "Test.Backbuffer" };
    inline constexpr loom::PassKey k_First{ "Test.First" };

    inline constexpr loom::RunPolicy k_EveryFrame = loom::RunPolicy::EveryFrame;
    inline constexpr loom::QueueAffinity k_Graphics = loom::QueueAffinity::Graphics;

    inline loom::BufferDeclaration MakeBuffer()
    {
        return loom::BufferDeclaration
        {
            .Key = k_Data,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Size = loom::SizeSource::Bytes(64),
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid
        };
    }

    inline loom::ImageDeclaration MakeImageOfTarget(loom::FrameTargetKey target)
    {
        return loom::ImageDeclaration
        {
            .Key = k_Albedo,
            .Lifetime = loom::ResourceLifetime::Frame,
            .Format = ImageFormat{ ImageComponentFormats::R8, ImageDataType::UNorm },
            .Extent = loom::ExtentSource::OfTarget(target, 0.5f),
            .Mips = loom::MipSource::One(),
            .Layers = 1,
            .Cpu = loom::CpuAccess::None,
            .HistoryDepth = 0,
            .NoHistoryAction = loom::OnNoHistory::Invalid
        };
    }

    // The error tests build a graph with exactly one broken statement, so the sink holds exactly one diagnostic.
    inline const loom::Diagnostic& OnlyDiagnostic(const loom::CollectingSink& sink)
    {
        EXPECT_EQ(sink.Diagnostics().size(), 1u);
        return sink.Diagnostics().front();
    }

}

#endif // !DIAMOND_DOGS_LOOM_TESTS_FREEZE_TEST_HELPERS_HPP
