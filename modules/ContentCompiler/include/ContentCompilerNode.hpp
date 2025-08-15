#pragma once
#ifndef CONTENT_COMPILER_NODE_HPP
#define CONTENT_COMPILER_NODE_HPP
#include "ContentCompilerCore.hpp"
#include "ContentCompilerTypes.hpp"
#include <thread>

namespace ContentCompiler
{

// Base class for all graph nodes
class GraphNode
{
public:
    explicit GraphNode(NodeId id) : nodeId(id) {}
    virtual ~GraphNode() = default;

    // Pure virtual - each node must implement its processing logic
    virtual Task<NodeResult> Execute(NodeInputs inputs, std::stop_token stopToken) = 0;
    
    // Node metadata for introspection and visual editor
    virtual NodeMetadata GetMetadata() const = 0;
    virtual std::vector<PinDescriptor> GetInputPins() const = 0;
    virtual std::vector<PinDescriptor> GetOutputPins() const = 0;
    
    // Node identification
    NodeId GetId() const noexcept
    {
        return nodeId;
    }
    
    // Validate inputs before execution
    virtual bool ValidateInputs(const NodeInputs& inputs) const
    {
        auto inputPins = GetInputPins();
        for (const auto& pin : inputPins)
        {
            if (pin.isRequired && !inputs.HasInput(pin.id))
            {
                return false;
            }
        }
        return true;
    }
    
    // Estimate processing time for scheduling hints
    virtual std::chrono::milliseconds EstimateProcessingTime() const
    {
        return std::chrono::milliseconds{100}; // Default estimate
    }

protected:
    // Helper for setting node context in coroutines
    void setNodeContext()
    {
        Task<void>::promise_type::setCurrentNodeId(nodeId);
    }
    
    // Helper for creating error results
    NodeResult CreateErrorResult(const std::string& message) const
    {
        NodeResult result;
        result.SetError(message);
        return result;
    }
    
    // Helper for checking cancellation in long operations
    bool ShouldCancel(const std::stop_token& stopToken) const
    {
        return stopToken.stop_requested();
    }

private:
    NodeId nodeId;
};

// Utility base class for simple synchronous nodes
class SyncGraphNode : public GraphNode
{
public:
    explicit SyncGraphNode(NodeId id) : GraphNode(id) {}
    
    // Implement Execute to call ProcessSync
    Task<NodeResult> Execute(NodeInputs inputs, std::stop_token stopToken) override final
    {
        setNodeContext();
        
        if (!ValidateInputs(inputs))
        {
            co_return CreateErrorResult("Input validation failed");
        }
        
        if (ShouldCancel(stopToken))
        {
            co_return CreateErrorResult("Operation cancelled");
        }
        
        try
        {
            co_return ProcessSync(std::move(inputs), stopToken);
        }
        catch (const std::exception& e)
        {
            co_return CreateErrorResult(std::string("Exception: ") + e.what());
        }
    }
    
protected:
    // Derived classes implement this instead of Execute for simple sync operations
    virtual NodeResult ProcessSync(NodeInputs inputs, std::stop_token stopToken) = 0;
};

// Utility base class for file I/O nodes
class FileIONode : public GraphNode
{
public:
    explicit FileIONode(NodeId id) : GraphNode(id) {}
    
protected:
    // Helper for async file reading with cancellation
    Task<std::vector<uint8_t>> ReadFileAsync(const std::string& path, std::stop_token stopToken);
    
    // Helper for async file writing with cancellation  
    Task<bool> WriteFileAsync(const std::string& path, const std::vector<uint8_t>& data, std::stop_token stopToken);
    
    // Check if file exists
    bool FileExists(const std::string& path) const;
    
    // Get file size
    size_t GetFileSize(const std::string& path) const;
};

} // namespace ContentCompiler

#endif // !CONTENT_COMPILER_NODE_HPP
