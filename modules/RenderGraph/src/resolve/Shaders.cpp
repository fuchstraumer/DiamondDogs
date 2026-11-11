#include "resolve/ResolveContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "ShaderBundle.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestIndex.hpp"
#include <optional>
#include <string_view>

// LM201: the module or an entry point is not in the bundle. LM202: an entry point's stage does not fit the pass kind.
namespace loom::resolve
{

    std::optional<PassShader> FindPassShader(ResolveContext& context, const FrozenPass& pass)
    {
        // Not written yet (the author's part). The Resolve tests for LM201 and LM202 are disabled until it is.
        (void)context;
        (void)pass;
        return std::nullopt;
    }

}
