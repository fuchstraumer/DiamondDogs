#include "bind/Params.hpp"
#include "bind/BindContext.hpp"
#include "bind/Layout.hpp"
#include "bind/BoundGraph.hpp"
#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "Records.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "ShaderManifestLayout.hpp"
#include <algorithm>
#include <cstdint>
#include <string_view>

// LM305: the Params struct does not fit the entry point constants. The generated Params<CookProfile> already asserts
// each offset at compile time, so a size check is enough: it catches a Params type of the wrong profile, or a struct
// that is not the generated one. LM302: the constants hold caller data and the pass has no Params statement.
namespace loom::bind
{

namespace
{
    // The name that LM302 and LM305 report: the constants have no binding name of their own.
    constexpr std::string_view k_ParamsName{ "Params" };

    // A buffer device address or a descriptor handle that Loom writes into the push constants
    constexpr uint32_t k_StoredValueBytes = 8u;

    Diagnostic MakeParamsDiagnostic(DiagnosticCode code, const FrozenPass& pass, std::source_location where)
    {
        return Diagnostic
        {
            .Code = code,
            .Severity = DefaultSeverityOf(code),
            .Where = where,
            .Pass = pass.Key,
            .Binding = k_ParamsName,
        };
    }

    DiagnosticValue ByteSizeValue(uint32_t byte_size)
    {
        return DiagnosticValue
        {
            .FieldName = "ByteSize",
            .Kind = DiagnosticValueKind::Integer,
            .Integer = static_cast<int64_t>(byte_size),
        };
    }

    // The bytes of the constants that the caller fills: all of them, less the stored values that Loom writes. Only
    // push constants are counted: a target that delivers constants in a uniform buffer is not cooked yet.
    uint32_t CallerBytes(const PassLayout& layout, const lodestone::manifest::EntryPointConstantsView& constants)
    {
        const auto inPushConstants = [](const lodestone::manifest::MergedResource& merged)
        {
            return merged.Resource.ValueContainer() == lodestone::StoredValueContainer::PushConstants;
        };

        const uint32_t storedValues = static_cast<uint32_t>(std::ranges::count_if(layout.Resources, inPushConstants));
        const uint32_t storedBytes = storedValues * k_StoredValueBytes;
        return constants.ByteSize() > storedBytes ? constants.ByteSize() - storedBytes : 0u;
    }

}

BoundParams BindPassParams(BindContext& context, const FrozenPass& pass, const PassLayout& layout)
{
    BoundParams result;
    const bool hasParams = !pass.Params.empty();

    if (!layout.Constants.has_value())
    {
        // Params on an entry point that has no constants: nothing to fill
        if (hasParams)
        {
            Diagnostic diag = MakeParamsDiagnostic(DiagnosticCode::LM305, pass, pass.Params.front().Where);
            diag.Expected = ByteSizeValue(0u);
            diag.Actual = ByteSizeValue(pass.Params.front().ByteSize);
            context.Sink.Report(diag);
        }

        return result;
    }

    const lodestone::manifest::EntryPointConstantsView& constants = *layout.Constants;
    result.Delivery = constants.Delivery();
    result.ByteSize = constants.ByteSize();

    if (!hasParams)
    {
        // constants that hold only stored values need nothing from the caller
        if (CallerBytes(layout, constants) > 0u)
        {
            context.Sink.Report(MakeParamsDiagnostic(DiagnosticCode::LM302, pass, pass.Shader.Where));
        }

        return result;
    }

    // Freeze allows one Params statement per pass (LM113)
    const ParamsRecord& params = pass.Params.front();
    result.ParamsIndex = 0;

    if (params.ByteSize != constants.ByteSize())
    {
        Diagnostic diag = MakeParamsDiagnostic(DiagnosticCode::LM305, pass, params.Where);
        diag.Expected = ByteSizeValue(constants.ByteSize());
        diag.Actual = ByteSizeValue(params.ByteSize);
        context.Sink.Report(diag);
    }

    return result;
}

}
