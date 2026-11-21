#include "freeze/FrozenGraph.hpp"
#include "resolve/ResolveContext.hpp"
#include "resolve/Helpers.hpp"
#include "resolve/ResolvedGraph.hpp"
#include "Declarations.hpp"
#include "Records.hpp"
#include "ShaderLibraryTypes.hpp"
#include "ShaderManifest.hpp"
#include "ShaderManifestFormat.hpp"
#include "ShaderManifestIndex.hpp"
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// The small pieces that the Resolve steps share: entry point names, one pin as a query constraint, axis names from the
// bundle, and the capability axis rule.
namespace loom::resolve
{

    std::vector<std::string_view> EntryPointNames(const FrozenPass& pass)
    {
        std::vector<std::string_view> names;
        names.emplace_back(pass.Shader.EntryPoint);

        if (!pass.Shader.FragmentEntryPoint.empty())
        {
            names.emplace_back(pass.Shader.FragmentEntryPoint);
        }

        return names;
    }

    lodestone::ManifestQueryBuilder WherePin(const lodestone::ManifestQueryBuilder& query,
                                             const PinRecord& pin,
                                             lodestone::PinMode mode)
    {
        const std::string_view axisName = pin.AxisName;
        // WhereWhenActive is used to apply the constraints only when an axis is actually active:
        // think of cases like wave sizing, where this only counts if wave ops are also enabled
        const bool required = (mode == lodestone::PinMode::Required);
        // use std::visit and std::decay_t to get the type being requested out of
        // the std::variant, reduces boilerplate a bit
        const auto addConstraint = [&](const auto& value) -> lodestone::ManifestQueryBuilder
        {
            using Value = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<Value, uint32_t> || std::is_same_v<Value, bool>)
            {
                return required ? query.Where(axisName, value) : query.WhereWhenActive(axisName, value);
            }
            else if constexpr (std::is_same_v<Value, AxisValue::EnumName>)
            {
                return required ? query.Where(axisName, lodestone::AxisValueDomain::Enum, value.Name)
                                : query.WhereWhenActive(axisName, lodestone::AxisValueDomain::Enum, value.Name);
            }
            else if constexpr (std::is_same_v<Value, AxisValue::InterfaceName>)
            {
                return required ? query.Where(axisName, lodestone::AxisValueDomain::Type, value.Name)
                                : query.WhereWhenActive(axisName, lodestone::AxisValueDomain::Type, value.Name);
            }
            else
            {
                // monostate: a pin with no value constrains nothing
                return query;
            }
        };

        return std::visit(addConstraint, pin.Value.Value);
    }

    std::string_view AxisName(const lodestone::manifest::ModuleView& module, uint32_t local_axis)
    {
        return module.Bundle().String(module.AxisData(local_axis).NameString);
    }

    bool PrefersCapableValue(const lodestone::manifest::ModuleView& module, uint32_t local_axis)
    {
        // axes classified as device capabilities are (usually) boolean flags, so if we support
        // such a capability - then we want to try and use it!
        const lodestone::manifest::Axis& axis = module.AxisData(local_axis);
        return axis.Kind == lodestone::AxisKind::Capability && axis.Domain == lodestone::AxisValueDomain::Boolean;
    }

}
