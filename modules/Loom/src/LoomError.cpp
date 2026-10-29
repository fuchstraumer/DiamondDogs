#include "../include/LoomError.hpp" // what
#include <array>
#include <cstddef>
#include <string_view>

namespace loom
{

    namespace
    {

        struct CatalogEntry
        {
            loom::DiagnosticCode Code;
            std::string_view Name;
            loom::DiagnosticSeverity Severity;
            std::string_view Summary;
        };

        using enum loom::DiagnosticCode;
        constexpr loom::DiagnosticSeverity k_Error = loom::DiagnosticSeverity::Error;
        constexpr loom::DiagnosticSeverity k_Warning = loom::DiagnosticSeverity::Warning;

        // Sorted by code. The static_asserts below check the order, so a lookup can stop early.
        constexpr std::array k_Catalog
        {
            CatalogEntry{ LM101, "LM101", k_Error,
                              "A required field is Invalid." },
            CatalogEntry{ LM102, "LM102", k_Error,
                              "A key has two publishers." },
            CatalogEntry{ LM103, "LM103", k_Error,
                              "A key is used and never declared, imported, or published." },
            CatalogEntry{ LM104, "LM104", k_Error,
                              "An anchor is used and never declared." },
            CatalogEntry{ LM105, "LM105", k_Error,
                              "A pass does not have exactly one shader, or a Record pass does not have exactly one recorder." },
            CatalogEntry{ LM106, "LM106", k_Error,
                              "A pass does not have exactly one dispatch or draw statement." },
            CatalogEntry{ LM107, "LM107", k_Error,
                              "A key is declared with two kinds." },
            CatalogEntry{ LM108, "LM108", k_Error,
                              "A graph pin is set twice." },
            CatalogEntry{ LM109, "LM109", k_Error,
                              "A value is out of range." },

            CatalogEntry{ LM201, "LM201", k_Error,
                              "Unknown module or entry point." },
            CatalogEntry{ LM202, "LM202", k_Error,
                               "The shader stage does not match the pass kind." },
            CatalogEntry{ LM203, "LM203", k_Error,
                               "No variant matches the pins." },
            CatalogEntry{ LM204, "LM204", k_Error,
                               "More than one variant matches the pins." },
            CatalogEntry{ LM205, "LM205", k_Warning,
                               "An axis is unpinned and the device does not decide it. The declared default is used." },

            CatalogEntry{ LM301, "LM301", k_Error,
                               "A Bind name is in no variant of the entry point." },
            CatalogEntry{ LM302, "LM302", k_Error,
                               "A binding of the selected variant has no Bind." },
            CatalogEntry{ LM303, "LM303", k_Error,
                               "A narrowing widens the reflected access." },
            CatalogEntry{ LM304, "LM304", k_Error,
                               "The resource kind does not match the binding." },
            CatalogEntry{ LM305, "LM305", k_Error,
                               "A params struct field differs from the reflected member in name, offset, or size." },
            CatalogEntry{ LM306, "LM306", k_Error,
                               "BindPrevious on a resource with HistoryDepth 0." },

            CatalogEntry{ LM401, "LM401", k_Error,
                               "Two bindings derive different sizes for one resource." },
            CatalogEntry{ LM402, "LM402", k_Error,
                               "A derived size or extent has no source. No binding states it." },
            CatalogEntry{ LM403, "LM403", k_Error,
                               "The format does not fit the binding." },

            CatalogEntry{ LM501, "LM501", k_Error,
                               "The passes make a cycle." },
            CatalogEntry{ LM502, "LM502", k_Error,
                               "A hazard has no ordering path between its passes." },
            CatalogEntry{ LM503, "LM503", k_Error,
                               "A read comes before any write in the frame, with no BindPrevious and no initial data." },
            CatalogEntry{ LM504, "LM504", k_Warning,
                               "A write is never read." },
            CatalogEntry{ LM505, "LM505", k_Warning,
                               "A pass has no path to a root output." },

            CatalogEntry{ LM601, "LM601", k_Error,
                               "A device limit is exceeded." },
            CatalogEntry{ LM602, "LM602", k_Error,
                               "The color targets do not match the attachments." },
            CatalogEntry{ LM603, "LM603", k_Error,
                               "A vertex input semantic is not found." },

            CatalogEntry{ LM701, "LM701", k_Error,
                               "A supplied future does not match its ImportDeclaration." },
            CatalogEntry{ LM702, "LM702", k_Error,
                               "A future failed. The passes that depend on it stop permanently." },
            CatalogEntry{ LM703, "LM703", k_Error,
                               "A params key was never set before the tick." },
            CatalogEntry{ LM704, "LM704", k_Error,
                               "Set was called with a type other than the key's type." },
        };

        consteval bool CatalogIsSorted()
        {
            for (size_t i = 1; i < k_Catalog.size(); ++i)
            {
                if (k_Catalog[i - 1].Code >= k_Catalog[i].Code)
                {
                    return false;
                }

            }

            return true;
        }

        static_assert(CatalogIsSorted(), "k_Catalog must be sorted by code, with no duplicates");

        // Catches a copy-paste slip where the name and the code disagree.
        consteval bool CatalogNamesMatchCodes()
        {
            for (const CatalogEntry& entry : k_Catalog)
            {
                uint16_t value = static_cast<uint16_t>(entry.Code);

                bool nameMatches = entry.Name.size() == 5 && entry.Name[0] == 'L' && entry.Name[1] == 'M' &&
                    entry.Name[2] == static_cast<char>('0' + value / 100) &&
                    entry.Name[3] == static_cast<char>('0' + value / 10 % 10) &&
                    entry.Name[4] == static_cast<char>('0' + value % 10);

                if (!nameMatches)
                {
                    return false;
                }

            }

            return true;
        }

        static_assert(CatalogNamesMatchCodes(), "a k_Catalog entry's name does not match its code");

        const CatalogEntry* Find(DiagnosticCode code) noexcept
        {
            for (const CatalogEntry& entry : k_Catalog)
            {
                if (entry.Code == code)
                {
                    return &entry;
                }

                if (entry.Code > code)
                {
                    break;
                }

            }

            return nullptr;
        }

    }

    std::string_view ToString(DiagnosticCode code) noexcept
    {
        const CatalogEntry* entry = Find(code);
        return entry != nullptr ? entry->Name : "Invalid";
    }

    std::string_view ToString(DiagnosticStage stage) noexcept
    {
        switch (stage)
        {
        case DiagnosticStage::Invalid:
            return "Invalid";
        case DiagnosticStage::Freeze: 
            return "Freeze";
        case DiagnosticStage::Resolve:
            return "Resolve";
        case DiagnosticStage::Bind:   
            return "Bind";
        case DiagnosticStage::Size:   
            return "Size";
        case DiagnosticStage::Order:   
            return "Order";
        case DiagnosticStage::Layout:  
            return "Layout";
        case DiagnosticStage::Runtime: 
            return "Runtime";
        }

    }

    std::string_view ToString(DiagnosticSeverity severity) noexcept
    {
        switch (severity)
        {
        case DiagnosticSeverity::Invalid:
            return "Invalid";
        case DiagnosticSeverity::Note:
            return "Note";
        case DiagnosticSeverity::Warning:
            return "Warning";
        case DiagnosticSeverity::Error:
            return "Error";
        case DiagnosticSeverity::Fatal:
            return "Fatal";
        }

    }

    std::string_view SummaryOf(DiagnosticCode code) noexcept
    {
        const CatalogEntry* entry = Find(code);
        return entry != nullptr ? entry->Summary : "Invalid";
    }

    DiagnosticSeverity DefaultSeverityOf(DiagnosticCode code) noexcept
    {
        const CatalogEntry* entry = Find(code);
        return entry != nullptr ? entry->Severity : DiagnosticSeverity::Invalid;
    }

}
