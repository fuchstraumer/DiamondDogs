#pragma once
#ifndef DIAMOND_DOGS_LOOM_BIND_HELPERS_HPP
#define DIAMOND_DOGS_LOOM_BIND_HELPERS_HPP
#include "PassStatements.hpp"
#include "Records.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifestLayout.hpp"
#include <string_view>

// Helpers.cpp: the small conversions that more than one Bind step uses.
namespace loom::bind
{

    // The name that Bind("name", key) matches. Stub for Lodestone request C26: a shim in an entry point parameter
    // reflects as the shim's inner field ("data") in the scope "entryPointParams_<parameter>", so Loom takes the
    // parameter name from the scope.
    [[nodiscard]] std::string_view BindingNameOf(const lodestone::manifest::ResolvedResource& resource);

    // The binding that `resource` is a view of, or empty when it is a root. A view is another typed binding of the
    // same memory: an atomic buffer and a plain read-only binding of it, for example. Stub for Lodestone request C27:
    // the shader states it as the tag "ViewOf:<root binding name>".
    [[nodiscard]] std::string_view ViewRootOf(const lodestone::manifest::ResolvedResource& resource);

    // Lodestone's reflected access as Order's access. A uniform buffer and a sampler reflect no access: both are
    // reads. Append, consume and rasterizer-ordered access read and write.
    [[nodiscard]] ResourceAccess ToResourceAccess(lodestone::ResourceAccess access);

    // The access that a narrowing leaves. No narrowing keeps the reflected access.
    [[nodiscard]] ResourceAccess NarrowedAccess(ResourceAccess reflected, BindNarrowing narrowing);

    // True when a key of `kind` can fill a binding of `binding_kind`.
    [[nodiscard]] bool KeyFitsBinding(ResourceKind kind, lodestone::BindingKind binding_kind);

}

#endif // !DIAMOND_DOGS_LOOM_BIND_HELPERS_HPP
