#include "resolve/ResolveContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestDevice.hpp"
#include "ShaderManifestIndex.hpp"
#include <optional>
#include <span>

// The query from the merged pins, the device filter, then one variant. LM203: no variant matches the pins. LM204: more
// than one still matches. LM205: an unpinned axis takes its declared default (a warning). LM207: variants match the
// pins, and the device can run none of them.
namespace loom::resolve
{

    std::optional<lodestone::VariantKey> SelectVariant(ResolveContext& context,
                                                       const FrozenPass& pass,
                                                       const PassShader& shader,
                                                       std::span<const MergedPin> pins)
    {
        // Not written yet (the author's part). The Resolve tests for LM203 to LM207 are disabled until it is.
        (void)context;
        (void)pass;
        (void)shader;
        (void)pins;
        return std::nullopt;
    }

}
