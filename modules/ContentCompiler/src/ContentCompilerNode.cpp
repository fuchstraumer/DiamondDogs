#include "ContentCompilerNode.hpp"
#include <filesystem>
#include <fstream>
#include <thread>

namespace ContentCompiler
{

// FileIONode implementation
Task<std::vector<uint8_t>> FileIONode::ReadFileAsync(const std::string& path, std::stop_token stopToken)
{
    setNodeContext();
    
    if (ShouldCancel(stopToken))
    {
        co_return std::vector<uint8_t>{};
    }
    
    // Check if file exists first
    if (!FileExists(path))
    {
        throw std::runtime_error("File does not exist: " + path);
    }
    
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open file: " + path);
    }
    
    const auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    
    // Read in chunks to allow for cancellation on large files
    constexpr size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
    size_t totalRead = 0;
    
    while (totalRead < buffer.size() && !ShouldCancel(stopToken))
    {
        const size_t remainingBytes = buffer.size() - totalRead;
        const size_t bytesToRead = std::min(CHUNK_SIZE, remainingBytes);
        
        file.read(reinterpret_cast<char*>(buffer.data() + totalRead), bytesToRead);
        totalRead += static_cast<size_t>(file.gcount());
        
        // Yield periodically to allow other coroutines to run
        if (totalRead % (CHUNK_SIZE * 4) == 0)
        {
            co_await std::suspend_always{};
        }
    }

    if (ShouldCancel(stopToken))
    {
        co_return std::vector<uint8_t>{};
    }
    
    co_return buffer;
}

Task<bool> FileIONode::WriteFileAsync(const std::string& path, const std::vector<uint8_t>& data, std::stop_token stopToken)
{
    setNodeContext();
    
    if (ShouldCancel(stopToken))
    {
        co_return false;
    }
    
    // Ensure directory exists
    std::filesystem::path filePath(path);
    if (filePath.has_parent_path())
{
        std::filesystem::create_directories(filePath.parent_path());
    }
    
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        throw std::runtime_error("Failed to create file: " + path);
    }
    
    // Write in chunks to allow for cancellation on large files
    constexpr size_t CHUNK_SIZE = 64 * 1024; // 64KB chunks
    size_t totalWritten = 0;
    
    while (totalWritten < data.size() && !ShouldCancel(stopToken))
    {
        const size_t remainingBytes = data.size() - totalWritten;
        const size_t bytesToWrite = std::min(CHUNK_SIZE, remainingBytes);
        
        file.write(reinterpret_cast<const char*>(data.data() + totalWritten), bytesToWrite);
        totalWritten += bytesToWrite;
        
        // Yield periodically
        if (totalWritten % (CHUNK_SIZE * 4) == 0)
        {
            co_await std::suspend_always{};
        }
    }
    
    if (ShouldCancel(stopToken))
    {
        co_return false;
    }
    
    file.flush();
    co_return file.good();
}

bool FileIONode::FileExists(const std::string& path) const
{
    return std::filesystem::exists(path);
}

size_t FileIONode::GetFileSize(const std::string& path) const
{
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<size_t>(size);
}

} // namespace ContentCompiler
