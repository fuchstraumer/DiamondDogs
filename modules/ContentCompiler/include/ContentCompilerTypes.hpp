#pragma once
#ifndef CONTENT_COMPILER_TYPES_HPP
#define CONTENT_COMPILER_TYPES_HPP
#include "utility/delegate.hpp"
#include <cstdint>
#include <any>
#include <vector>
#include <string>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <atomic>
#include <coroutine>
#include <glm/vec2.hpp>

namespace ContentCompiler
{

// Core identifier types
using NodeId = uint64_t;
using PinId = uint32_t;
using TypeHash = uint32_t;

// Forward declarations
class GraphNode;
class CreationGraph;
class GraphExecutor;
class NodeRegistry;
class GraphBuilder;
template<typename T> class Task;

// Data flow types
class NodeData;
class NodeInputs;
class NodeResult;

// Metadata types
struct PinDescriptor;
struct NodeMetadata;
struct GraphProgress;

// Callback types using delegate_t
using ProgressCallback = delegate_t<void(const GraphProgress&)>;
using NodeCompletionCallback = delegate_t<void(NodeId, const NodeResult&)>;
using ErrorCallback = delegate_t<void(const std::string&)>;

// Memory tracking for coroutines
class CoroutineAllocator;

// Type-safe data container for node inputs/outputs
class NodeData
{
private:
    std::any data;
    TypeHash typeHash;
    
public:
    NodeData() : typeHash(0) {}
    
    template<typename T>
    explicit NodeData(T&& value) 
        : data(std::forward<T>(value))
        , typeHash(typeid(std::decay_t<T>).hash_code()) 
    {}
    
    template<typename T>
    void Set(T&& value) {
        data = std::forward<T>(value);
        typeHash = typeid(std::decay_t<T>).hash_code();
    }
    
    template<typename T>
    T& Get()
    {
        if (typeHash != typeid(T).hash_code())
        {
            throw std::bad_any_cast();
        }
        return std::any_cast<T&>(data);
    }
    
    template<typename T>
    const T& Get() const
    {
        if (typeHash != typeid(T).hash_code())
        {
            throw std::bad_any_cast();
        }
        return std::any_cast<const T&>(data);
    }
    
    template<typename T>
    bool Is() const noexcept
    {
        return typeHash == typeid(T).hash_code();
    }

    TypeHash GetTypeHash() const noexcept
    {
        return typeHash;
    }
    
    bool HasValue() const noexcept
    {
        return data.has_value();
    }
    
    void Clear()
    {
        data.reset();
        typeHash = 0;
    }
};

// Collection of inputs for a node
class NodeInputs
{
private:
    std::unordered_map<PinId, NodeData> inputs;
    
public:
    template<typename T>
    T& GetInput(PinId pinId)
    {
        auto it = inputs.find(pinId);
        if (it == inputs.end())
        {
            throw std::out_of_range("Input pin not found");
        }
        return it->second.Get<T>();
    }
    
    template<typename T>
    const T& GetInput(PinId pinId) const
    {
        auto it = inputs.find(pinId);
        if (it == inputs.end())
        {
            throw std::out_of_range("Input pin not found");
        }
        return it->second.Get<T>();
    }
    
    bool HasInput(PinId pinId) const noexcept
    {
        return inputs.find(pinId) != inputs.end();
    }
    
    void SetInput(PinId pinId, NodeData data)
    {
        inputs[pinId] = std::move(data);
    }
    
    template<typename T>
    void SetInput(PinId pinId, T&& value)
    {
        inputs[pinId] = NodeData(std::forward<T>(value));
    }

    std::vector<PinId> GetAvailableInputs() const
    {
        std::vector<PinId> result;
        result.reserve(inputs.size());
        for (const auto& [pinId, _] : inputs)
        {
            result.push_back(pinId);
        }
        return result;
    }
    
    void Clear()
    {
        inputs.clear();
    }
};

// Result from node execution
class NodeResult
{
private:
    std::unordered_map<PinId, NodeData> outputs;
    bool success = true;
    std::string errorMessage;
    
public:
    template<typename T>
    void SetOutput(PinId pinId, T&& value)
    {
        outputs[pinId] = NodeData(std::forward<T>(value));
    }
    
    template<typename T>
    T& GetOutput(PinId pinId)
    {
        auto it = outputs.find(pinId);
        if (it == outputs.end())
        {
            throw std::out_of_range("Output pin not found");
        }
        return it->second.Get<T>();
    }
    
    template<typename T>
    const T& GetOutput(PinId pinId) const
    {
        auto it = outputs.find(pinId);
        if (it == outputs.end())
        {
            throw std::out_of_range("Output pin not found");
        }
        return it->second.Get<T>();
    }
    
    bool HasOutput(PinId pinId) const noexcept
    {
        return outputs.find(pinId) != outputs.end();
    }

    NodeData& GetOutputData(PinId pinId)
    {
        auto it = outputs.find(pinId);
        if (it == outputs.end())
        {
            throw std::out_of_range("Output pin not found");
        }
        return it->second;
    }

    const NodeData& GetOutputData(PinId pinId) const
    {
        auto it = outputs.find(pinId);
        if (it == outputs.end())
        {
            throw std::out_of_range("Output pin not found");
        }
        return it->second;
    }
    
    std::vector<PinId> GetAvailableOutputs() const
    {
        std::vector<PinId> result;
        result.reserve(outputs.size());
        for (const auto& [pinId, _] : outputs)
        {
            result.push_back(pinId);
        }
        return result;
    }
    
    bool IsSuccess() const noexcept
    {
        return success;
    }

    void SetError(const std::string& message)
    {
        success = false;
        errorMessage = message;
    }

    const std::string& GetErrorMessage() const noexcept
    {
        return errorMessage;
    }

    void Clear()
    {
        outputs.clear();
        success = true;
        errorMessage.clear();
    }
};

// Describes input/output pins on nodes
struct PinDescriptor
{
    PinId id;
    std::string name;
    TypeHash typeHash;
    bool isRequired = true;
    std::optional<NodeData> defaultValue;
    std::string description;
    
    PinDescriptor(PinId pinId, const std::string& pinName, TypeHash type)
        : id(pinId), name(pinName), typeHash(type) {}
        
    template<typename T>
    PinDescriptor(PinId pinId, const std::string& pinName, T&& defaultVal)
        : id(pinId), name(pinName), typeHash(typeid(std::decay_t<T>).hash_code())
        , isRequired(false), defaultValue(NodeData(std::forward<T>(defaultVal))) {}
};

// Metadata for visual editor and introspection
struct NodeMetadata
{
    std::string name;
    std::string description;
    std::string category;
    float editorPosition[2] = {0.0f, 0.0f}; // For visual editor layout
    uint32_t color = 0xFFFFFFFF; // RGBA color for visual theming
    bool isAsync = true; // Whether this node runs asynchronously
    std::chrono::milliseconds estimatedDuration{0}; // Hint for scheduling
};

// Progress tracking for UI and monitoring
struct GraphProgress
{
    size_t totalNodes = 0;
    size_t completedNodes = 0;
    NodeId currentlyExecutingNode = 0;
    std::string currentOperation;
    float percentComplete = 0.0f;
    std::chrono::steady_clock::time_point startTime;
    std::chrono::milliseconds elapsedTime{0};
    
    // Memory statistics
    size_t totalMemoryAllocated = 0;
    size_t currentMemoryUsage = 0;
    size_t activeCoroutines = 0;
};

// Connection between nodes
struct NodeConnection
{
    NodeId fromNode;
    PinId fromPin;
    NodeId toNode;
    PinId toPin;
    
    bool operator==(const NodeConnection& other) const noexcept
    {
        return fromNode == other.fromNode && 
               fromPin == other.fromPin &&
               toNode == other.toNode &&
               toPin == other.toPin;
    }
};

// Request for graph execution - message passing interface
struct GraphExecutionRequest
{
    CreationGraph graph;
    std::unordered_map<std::string, NodeData> globalInputs;
    std::shared_ptr<class GraphExecutionReply> reply;
};

} // namespace ContentCompiler

#endif //!CONTENT_COMPILER_TYPES_HPP
