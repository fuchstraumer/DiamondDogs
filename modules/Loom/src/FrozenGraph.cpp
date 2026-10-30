#include "FrozenGraph.hpp"
#include "Declarations.hpp"
#include "LoomDiagnostic.hpp"
#include "LoomError.hpp"
#include "LoomKeys.hpp"
#include "Records.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <source_location>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace loom
{

    namespace
    {

        enum class KeyClass : uint8_t
        {
            Invalid = 0,
            Buffer = 1,         // the three resource classes all index FrozenGraph::Resources. They are separate so
            Image = 2,          // that one name as a buffer and as an image is LM107, not LM102.
            Sampler = 3,
            FrameTarget = 4,
            Anchor = 5,
            Pass = 6,
            Sequence = 7
        };

        struct KeyEntry
        {
            KeyClass Class{ KeyClass::Invalid };
            int32_t Index{ -1 };
            std::source_location Where;
        };

        struct FreezeContext
        {
            const GraphRecord& Record;
            DiagnosticSink& Sink;
            FrozenGraph Graph;
            std::unordered_map<uint64_t, KeyEntry> Keys;
        };

        Diagnostic MakeDiagnostic(DiagnosticCode code, std::source_location where)
        {
            return Diagnostic
            {
                .Code = code,
                .Severity = DiagnosticSeverity::Error,
                .Where = where,
            };
        }

        void ReportMissing(FreezeContext& context, std::string_view field_name, std::source_location where)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM101, where);
            diag.Actual.FieldName = field_name;
            context.Sink.Report(diag);
        }

        void ReportOutOfRange(FreezeContext& context, std::string_view field_name, int64_t value,
                              std::source_location where)
        {
            Diagnostic diag = MakeDiagnostic(DiagnosticCode::LM109, where);
            diag.Actual = DiagnosticValue{ .FieldName = field_name,
                                           .Kind = DiagnosticValueKind::Integer,
                                           .Text = {},
                                           .Integer = value };
            context.Sink.Report(diag);
        }

        bool Publish(FreezeContext& context, KeyRef key, KeyClass key_class, int32_t index,
                     std::source_location where)
        {
            // initial publish, checking just the key first
            const auto found = context.Keys.find(key.Hash);
            if (found == context.Keys.end())
            {
                context.Keys.emplace(key.Hash, KeyEntry{ .Class = key_class, .Index = index, .Where = where });
                return true;
            }

            const bool duplicateNameOfClass = found->second.Class == key_class;
            // either duplicate name of the same class, or collision of having name assigned twice to different types
            DiagnosticCode errorCode = duplicateNameOfClass ? DiagnosticCode::LM102 : DiagnosticCode::LM107;
            Diagnostic diag = MakeDiagnostic(errorCode, where);
            diag.OtherWhere = found->second.Where;
            diag.Resource = key;
            context.Sink.Report(diag);
            return false;
        }

        void PublishBuiltinAnchor(FreezeContext& context, AnchorKey anchor)
        {
            const int32_t anchorIndex = static_cast<int32_t>(context.Graph.Anchors.size());

            // A built-in has no call site, so a caller's DeclareAnchor of the same name reports against this one.
            if (!Publish(context, anchor.ToRef(), KeyClass::Anchor, anchorIndex, std::source_location{}))
            {
                return;
            }

            context.Graph.Anchors.emplace_back(anchor.ToRef(),
                                               AnchorOrigin::Builtin,
                                               SequenceIndex{},
                                               -1,
                                               std::source_location{});
        }

        // Runs first, on an empty graph, so the two anchors land at k_BeginFrameAnchorIndex and k_EndFrameAnchorIndex.
        void PublishBuiltinAnchors(FreezeContext& context)
        {
            PublishBuiltinAnchor(context, BeginFrameAnchor);
            PublishBuiltinAnchor(context, EndFrameAnchor);
        }

        void PublishBuffers(FreezeContext& context)
        {
            for (const DeclarationRecord<BufferDeclaration>& decl : context.Record.Buffers)
            {
                const int32_t resourceIndex = static_cast<int32_t>(context.Graph.Resources.size());
                const KeyRef key = decl.Value.Key.ToRef();
                if (!Publish(context, key, KeyClass::Buffer, resourceIndex, decl.Where))
                {
                    continue; // don't publish: diag sink on the context will have already reported the issue
                }

                FrozenResource resource
                {
                    .Key=key,
                    .Kind=ResourceKind::Buffer,
                    .Origin=ResourceOrigin::Declared,
                    .DeclarationIndex=static_cast<int32_t>(context.Graph.Buffers.size()),
                    .Where=decl.Where
                };

                context.Graph.Resources.emplace_back(resource);
                context.Graph.Buffers.emplace_back(decl.Value);
            }
        }

        void PublishImages(FreezeContext& context)
        {
            for (const DeclarationRecord<ImageDeclaration>& decl : context.Record.Images)
            {
                const int32_t resourceIndex = static_cast<int32_t>(context.Graph.Resources.size());
                const KeyRef key = decl.Value.Key.ToRef();
                if (!Publish(context, key, KeyClass::Image, resourceIndex, decl.Where))
                {
                    continue;
                }

                FrozenResource resource
                {
                    .Key=key,
                    .Kind=ResourceKind::Image,
                    .Origin=ResourceOrigin::Declared,
                    .DeclarationIndex=static_cast<int32_t>(context.Graph.Images.size()),
                    .Where=decl.Where
                };

                context.Graph.Resources.emplace_back(resource);
                
                FrozenImage image
                {
                    .Declaration=decl.Value,
                    .ExtentTarget={} // can't set extent target during the publish pass - leave it out
                };
                context.Graph.Images.emplace_back(image);
            }
        }

        void PublishSamplers(FreezeContext& context)
        {
            for (const DeclarationRecord<SamplerDeclaration>& decl : context.Record.Samplers)
            {
                const int32_t resourceIndex = static_cast<int32_t>(context.Graph.Resources.size());
                const KeyRef key = decl.Value.Key.ToRef();
                if (!Publish(context, key, KeyClass::Sampler, resourceIndex, decl.Where))
                {
                    continue;
                }

                FrozenResource resource
                {
                    .Key=key,
                    .Kind=ResourceKind::Sampler,
                    .Origin=ResourceOrigin::Declared,
                    .DeclarationIndex=static_cast<int32_t>(context.Graph.Samplers.size()),
                    .Where=decl.Where
                };

                context.Graph.Resources.emplace_back(resource);
                context.Graph.Samplers.emplace_back(decl.Value);
            }
        }

        void PublishImports(FreezeContext& context)
        {
            for (const DeclarationRecord<ImportDeclaration>& decl : context.Record.Imports)
            {
                const int32_t resourceIndex = static_cast<int32_t>(context.Graph.Resources.size());
                const KeyRef key = decl.Value.Key.ToRef();
                if (!Publish(context, key, KeyClass::Image, resourceIndex, decl.Where))
                {
                    continue;
                }

                // Only images can be imported today; ImportDeclaration's key is a ResourceKey<Image>.
                FrozenResource resource
                {
                    .Key=key,
                    .Kind=ResourceKind::Image,
                    .Origin=ResourceOrigin::Imported,
                    .DeclarationIndex=static_cast<int32_t>(context.Graph.Imports.size()),
                    .Where=decl.Where
                };

                context.Graph.Resources.emplace_back(resource);
                context.Graph.Imports.emplace_back(decl.Value);
            }
        }

        void PublishAnchors(FreezeContext& context)
        {
            for (const AnchorDeclarationRecord& decl : context.Record.Anchors)
            {
                const int32_t anchorIndex = static_cast<int32_t>(context.Graph.Anchors.size());

                if (!Publish(context, decl.Anchor, KeyClass::Anchor, anchorIndex, decl.Where))
                {
                    continue; // don't publish: diag sink on the context will have already reported the issue
                }

                context.Graph.Anchors.emplace_back(decl.Anchor,
                                                   AnchorOrigin::Declared,
                                                   SequenceIndex{},
                                                   -1,
                                                   decl.Where);
            }
        }

        void PublishFrameTargets(FreezeContext& context)
        {
            for (const FrameTargetImportRecord& decl : context.Record.FrameTargets)
            {
                const int32_t targetIndex = static_cast<int32_t>(context.Graph.FrameTargets.size());

                if (!Publish(context, decl.Target, KeyClass::FrameTarget, targetIndex, decl.Where))
                {
                    continue;
                }

                context.Graph.FrameTargets.emplace_back(decl.Target, decl.Where);
            }
        }

        // An exposed anchor belongs to its sequence, so it is published from the sequence walk, not with the others.
        void PublishExposedAnchors(FreezeContext& context, const SequenceRecord& sequence_record,
                                   SequenceIndex sequence_index, FrozenSequence& sequence)
        {
            for (const ExposedAnchorRecord& exposed : sequence_record.ExposedAnchors)
            {
                const AnchorIndex anchorIndex{ static_cast<int32_t>(context.Graph.Anchors.size()) };

                if (!Publish(context, exposed.Anchor, KeyClass::Anchor, anchorIndex.Value, exposed.Where))
                {
                    continue;
                }

                context.Graph.Anchors.emplace_back(exposed.Anchor,
                                                   AnchorOrigin::Exposed,
                                                   sequence_index,
                                                   exposed.Position,
                                                   exposed.Where);
                sequence.ExposedAnchors.push_back(anchorIndex);
            }
        }

        // FirstPass, PassCount and the anchor uses are filled in after the passes are flattened.
        void PublishSequences(FreezeContext& context)
        {
            for (const SequenceRecord& decl : context.Record.Sequences)
            {
                const SequenceIndex sequenceIndex{ static_cast<int32_t>(context.Graph.Sequences.size()) };

                if (!Publish(context, decl.Key, KeyClass::Sequence, sequenceIndex.Value, decl.Where))
                {
                    continue;
                }

                FrozenSequence sequence{};
                sequence.Key = decl.Key;
                sequence.Where = decl.Where;

                PublishExposedAnchors(context, decl, sequenceIndex, sequence);
                context.Graph.Sequences.push_back(std::move(sequence));
            }
        }

        // Where one pass came from in the record. Freeze flattens top-level and nested passes into one list.
        struct PassSource
        {
            const PassRecord* Pass{ nullptr };
            // The sequence's index in GraphRecord::Sequences. It equals its index in FrozenGraph::Sequences unless
            // PublishSequences skipped a duplicate, and that reported an error, so the graph is discarded anyway.
            SequenceIndex Sequence;
            int32_t PositionInSequence{ -1 };
        };

        bool EarlierDeclared(const PassSource& left, const PassSource& right) noexcept
        {
            return left.Pass->DeclarationOrder < right.Pass->DeclarationOrder;
        }

        std::vector<PassSource> CollectPasses(const GraphRecord& record)
        {
            std::vector<PassSource> sources;

            for (const PassRecord& pass : record.Passes)
            {
                sources.push_back(PassSource{ .Pass = &pass, .Sequence = {}, .PositionInSequence = -1 });
            }

            for (int32_t sequence = 0; sequence < static_cast<int32_t>(record.Sequences.size()); ++sequence)
            {
                const std::vector<PassRecord>& passes = record.Sequences[sequence].Passes;

                for (int32_t position = 0; position < std::ssize(passes); ++position)
                {
                    sources.push_back(PassSource{ .Pass = &passes[position],
                                                  .Sequence = SequenceIndex{ sequence },
                                                  .PositionInSequence = position });
                }
            }

            std::ranges::sort(sources, EarlierDeclared);
            return sources;
        }

        // A pass's index is its place in the sorted list, which is its DeclarationOrder. The FrozenPass rows are
        // written later, one per source in this same order, so the indices published here stay true.
        void PublishPasses(FreezeContext& context, const std::vector<PassSource>& sources)
        {
            for (int32_t passIndex = 0; passIndex < static_cast<int32_t>(sources.size()); ++passIndex)
            {
                const PassRecord& pass = *sources[passIndex].Pass;
                Publish(context, pass.Key, KeyClass::Pass, passIndex, pass.Where);
            }
        }


    }

    std::optional<FrozenGraph> Freeze(const GraphRecord& record, DiagnosticSink& sink)
    {
        const int32_t errorsBefore = sink.ErrorCount();
        FreezeContext context{ .Record = record, .Sink = sink, .Graph = FrozenGraph{}, .Keys = {} };

        context.Graph.Name = record.Name;
        context.Graph.ShaderLibraryName = record.ShaderLibraryName;

        PublishBuiltinAnchors(context);
        PublishBuffers(context);
        PublishImages(context);
        PublishSamplers(context);
        PublishImports(context);
        PublishFrameTargets(context);
        PublishAnchors(context);
        PublishSequences(context);

        const std::vector<PassSource> passSources = CollectPasses(record);
        PublishPasses(context, passSources);


        // then check section

        // then collectpasses

        // freezepass

        if (sink.ErrorCount() > errorsBefore)
        {
            return std::nullopt;
        }

        return std::move(context.Graph);
    }

}
