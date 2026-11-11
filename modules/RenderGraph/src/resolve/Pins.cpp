#include "resolve/ResolveContext.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "ShaderManifestIndex.hpp"
#include <vector>

// One value per axis, the pass value first. A second Where on the same axis joins the values (1 then 2 means 1 or 2),
// so Loom merges the pins itself and builds one query. LM206: a graph pin that no resolved module declares.
namespace loom::resolve
{

    std::vector<MergedPin> MergePins(ResolveContext& context,
                                     const FrozenPass& pass,
                                     const lodestone::ManifestIndex& index)
    {
        // Not written yet (the author's part). The Resolve tests for the merge are disabled until it is.
        (void)context;
        (void)pass;
        (void)index;
        return {};
    }

    void CheckGraphPinsDeclared(ResolveContext& context)
    {
        // Not written yet (the author's part). The LM206 tests are disabled until it is.
        (void)context;
    }

}
