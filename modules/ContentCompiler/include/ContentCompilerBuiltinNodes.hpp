#pragma once
#ifndef CONTENT_COMPILER_BUILTIN_NODES_HPP
#define CONTENT_COMPILER_BUILTIN_NODES_HPP
#include "ContentCompilerNode.hpp"
#include <filesystem>

namespace ContentCompiler::BuiltinNodes
{

// Common pin IDs for consistency
namespace PinIds
{
    constexpr PinId FilePath = 1;
    constexpr PinId Data = 2;
    constexpr PinId Result = 3;
    constexpr PinId Success = 4;
    constexpr PinId Directory = 5;
    constexpr PinId Filename = 6;
    constexpr PinId Extension = 7;
    constexpr PinId MeshData = 10;
    constexpr PinId MaterialData = 11;
    constexpr PinId TextureData = 12;
}

// File I/O Nodes
class FileReaderNode : public FileIONode
{
public:
    explicit FileReaderNode(NodeId id) : FileIONode(id) {}
    
    Task<NodeResult> Execute(NodeInputs inputs, std::stop_token stopToken) override;
    NodeMetadata GetMetadata() const override;
    std::vector<PinDescriptor> GetInputPins() const override;
    std::vector<PinDescriptor> GetOutputPins() const override;
};

class FileWriterNode : public FileIONode
{
public:
    explicit FileWriterNode(NodeId id) : FileIONode(id) {}
    
    Task<NodeResult> Execute(NodeInputs inputs, std::stop_token stopToken) override;
    NodeMetadata GetMetadata() const override;
    std::vector<PinDescriptor> GetInputPins() const override;
    std::vector<PinDescriptor> GetOutputPins() const override;
};

// Utility Nodes
class SplitPathNode : public SyncGraphNode
{
public:
    explicit SplitPathNode(NodeId id) : SyncGraphNode(id) {}
    
    NodeResult ProcessSync(NodeInputs inputs, std::stop_token stopToken) override;
    NodeMetadata GetMetadata() const override;
    std::vector<PinDescriptor> GetInputPins() const override;
    std::vector<PinDescriptor> GetOutputPins() const override;
};

// Asset Processing Nodes
class ObjParserNode : public GraphNode
{
public:
    explicit ObjParserNode(NodeId id) : GraphNode(id) {}
    
    Task<NodeResult> Execute(NodeInputs inputs, std::stop_token stopToken) override;
    NodeMetadata GetMetadata() const override;
    std::vector<PinDescriptor> GetInputPins() const override;
    std::vector<PinDescriptor> GetOutputPins() const override;
    
    std::chrono::milliseconds EstimateProcessingTime() const override {
        return std::chrono::milliseconds{500}; // OBJ parsing can be slow
    }
};

class MaterialParserNode : public GraphNode
{
public:
    explicit MaterialParserNode(NodeId id) : GraphNode(id) {}
    
    Task<NodeResult> Execute(NodeInputs inputs, std::stop_token stopToken) override;
    NodeMetadata GetMetadata() const override;
    std::vector<PinDescriptor> GetInputPins() const override;
    std::vector<PinDescriptor> GetOutputPins() const override;
};

class TextureLoaderNode : public FileIONode
{
public:
    explicit TextureLoaderNode(NodeId id) : FileIONode(id) {}
    
    Task<NodeResult> Execute(NodeInputs inputs, std::stop_token stopToken) override;
    NodeMetadata GetMetadata() const override;
    std::vector<PinDescriptor> GetInputPins() const override;
    std::vector<PinDescriptor> GetOutputPins() const override;
    
    std::chrono::milliseconds EstimateProcessingTime() const override {
        return std::chrono::milliseconds{200}; // Texture loading/decoding
    }
};

// Batch processing node - processes arrays of data in parallel
class BatchNode : public GraphNode
{
public:
    explicit BatchNode(NodeId id) : GraphNode(id) {}
    
    Task<NodeResult> Execute(NodeInputs inputs, std::stop_token stopToken) override;
    NodeMetadata GetMetadata() const override;
    std::vector<PinDescriptor> GetInputPins() const override;
    std::vector<PinDescriptor> GetOutputPins() const override;
    
    std::chrono::milliseconds EstimateProcessingTime() const override {
        return std::chrono::milliseconds{1000}; // Variable based on batch size
    }
};

} // namespace ContentCompiler::BuiltinNodes

#endif // !CONTENT_COMPILER_BUILTIN_NODES_HPP
