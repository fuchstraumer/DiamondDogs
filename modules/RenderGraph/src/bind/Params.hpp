#pragma once
#ifndef DIAMOND_DOGS_LOOM_BIND_PARAMS_HPP
#define DIAMOND_DOGS_LOOM_BIND_PARAMS_HPP

// Params.cpp: whether the Params statement fits the entry point constants (LM305), and whether constants that hold
// caller data have a Params statement (LM302).
namespace loom
{
    struct BoundParams;
    struct FrozenPass;

    namespace bind
    {
        struct BindContext;
        struct PassLayout;

        [[nodiscard]] BoundParams BindPassParams(BindContext& context, const FrozenPass& pass, const PassLayout& layout);
    }
}

#endif // !DIAMOND_DOGS_LOOM_BIND_PARAMS_HPP
