#include "ResourceLoader.hpp"
#include <filesystem>
#include <algorithm>
#include <locale>
#include <execution>
#include <iostream>
#include "threading/ExponentialBackoffSleeper.hpp"
#include "threading/CriticalSection.hpp"

static std::mutex k_LogMutex;

ResourceLoader::ResourceLoader()
{
    Start();
}

ResourceLoader::~ResourceLoader()
{
    Stop();
}

void ResourceLoader::Subscribe(const char* file_type, FactoryFunctor func, DeleteFunctor del_fn)
{
    std::lock_guard subscribeGuard(subscribeMutex);
    if (factories.count(file_type) != 0 && deleters.count(file_type) != 0)
    {
        return;
    }
    factories[file_type] = func;
    deleters[file_type] = del_fn;
}

void ResourceLoader::Unsubscribe(const char* file_type)
{

}

void ResourceLoader::Load(const char* file_type, const char* file_path, void* _requester, SignalFunctor signal, void* user_data)
{
    namespace fs = std::filesystem;
    const std::string fileName{ file_path };
    const uint64_t fileNameHash{ std::hash<std::string>()(fileName) };

    {
        // Check to see if resource is already loaded
        std::lock_guard pendingDataGuard(pendingDataMutex);
        if (pendingResources.count(fileNameHash) != 0)
        {
            auto& listenersVec = pendingResourceListeners[fileNameHash];
            listenersVec.emplace_back(_requester, user_data);
            return;
        }
    }

    if (factories.count(file_type) == 0)
    {
        throw std::domain_error("Tried to load resource type for which there is no factory!");
    }

    if (deleters.count(file_type) == 0)
    {
        throw std::domain_error("No deleter function for current file type!");
    }

    ResourceData data;
    data.FileType = file_type;
    data.RefCount = 1;
    data.FileName = fileName;
    data.FileNameHash = fileNameHash;

    if (fs::exists(fileName))
    {
        data.AbsoluteFilePath = fs::canonical(fileName).string();
    }

    loadRequest req(data);
    req.requester = _requester;
    req.signal = signal;
    req.userData = user_data;

    if (resources.count(fileNameHash) != 0)
    {
        req.type = load_req_type::AlreadyLoaded;
    }
    else
    {
        std::lock_guard pendingResourcesGuard(pendingDataMutex);
        req.type = load_req_type::FreshLoad;
        pendingResources.emplace(fileNameHash);
    }

    {
        std::lock_guard queueGuard(queueMutex);
        requests.push_back(req);
    }
    cVar.notify_one();
}

void ResourceLoader::Load(const char* file_type, const char* file_name, const char* search_dir, void* _requester, SignalFunctor signal, void* user_data)
{
    namespace fs = std::filesystem;
    const std::string fileName{ file_name };
    const uint64_t fileNameHash{ std::hash<std::string>()(fileName) };

    {
        // Check to see if resource is already queued for load
        std::lock_guard pendingDataGuard(pendingDataMutex);
        if (pendingResources.count(fileNameHash) != 0)
        {
            auto& listenersVec = pendingResourceListeners[fileNameHash];
            listenersVec.emplace_back(_requester, user_data);
            return;
        }
    }

    if (factories.count(file_type) == 0)
    {
        throw std::domain_error("Tried to load resource type for which there is no factory!");
    }

    if (deleters.count(file_type) == 0)
    {
        throw std::domain_error("No deleter function for current file type!");
    }

    ResourceData data;
    data.FileType = file_type;
    data.RefCount = 1;
    data.FileName = fileName;
    data.FileNameHash = std::hash<std::string>()(data.FileName);
    data.SearchDir = std::string(search_dir);

    loadRequest req(data);
    req.requester = _requester;
    req.signal = signal;
    req.userData = user_data;

    if (resources.count(fileNameHash) != 0)
    {
        req.type = load_req_type::AlreadyLoaded;
    }
    else
    {
        std::lock_guard pendingResourcesGuard(pendingDataMutex);
        req.type = load_req_type::FreshLoad;
        pendingResources.emplace(fileNameHash);
    }

    {
        std::lock_guard queueGuard(queueMutex);
        requests.push_back(req);
    }
    cVar.notify_one();
}

void ResourceLoader::Unload(const char* file_type, const char* _path)
{
    namespace fs = std::filesystem;

    uint64_t pathHash = std::hash<std::string>()(_path);

    std::lock_guard queueGuard(queueMutex);
    if (auto iter = resources.find(pathHash); iter != std::end(resources))
    {
        --iter->second.RefCount;
        if (iter->second.RefCount == 0)
        {
            deleters.at(iter->second.FileType)(iter->second.Data, nullptr);
            resources.erase(iter);
        }
    }
}

ResourceLoader& ResourceLoader::GetResourceLoader()
{
    static ResourceLoader loader;
    return loader;
}

std::string ResourceLoader::FindFile(const std::string& fname, const std::string& init_dir, const size_t depth)
{
    namespace stdfs = std::filesystem;
    static std::unordered_map<std::string, stdfs::path> foundPathsCache;
    static CriticalSection cacheCS;

    auto caseInsensitiveComparison = [](const std::string & fname, const std::string & curr_entry)->bool
    {

        return std::equal(std::execution::par_unseq, fname.cbegin(), fname.cend(), curr_entry.cbegin(), curr_entry.cend(), [](const char a, const char b)
            {
                return std::tolower(a) == std::tolower(b);
            });
    };

    stdfs::path startingPath(stdfs::canonical(init_dir));

    stdfs::path fileNamePath(fname);
    fileNamePath = fileNamePath.filename();

    if (stdfs::exists(fileNamePath))
    {
        return fileNamePath.string();
    }

    {
        std::lock_guard cacheGuard(cacheCS);
        auto iter = foundPathsCache.find(fname);
        if (iter != foundPathsCache.end())
        {
            return iter->second.string();
        }
    }

    stdfs::path filePath = startingPath;

    for (size_t i = 0; i < depth; ++i)
    {
        filePath = filePath.parent_path();
    }

    for (auto& dirEntry : stdfs::recursive_directory_iterator(filePath))
    {
        if (dirEntry == startingPath)
        {
            continue;
        }

        if (!stdfs::is_regular_file(dirEntry) || stdfs::is_directory(dirEntry))
        {
            continue;
        }

        const stdfs::path entryPath(dirEntry);
        const std::string currEntryStr = entryPath.filename().string();

        if (caseInsensitiveComparison(fname, currEntryStr))
        {
            std::lock_guard cacheGuard(cacheCS);
            auto iter = foundPathsCache.emplace(fname, entryPath);
            return iter.first->second.string();
        }
    }

    return std::string();
}

void ResourceLoader::Start()
{
    shutdown = false;

    for (auto& thr : workers)
    {
        thr = std::thread(&ResourceLoader::workerFunction, this);
    }

    cVar.notify_all();
}

void ResourceLoader::Stop()
{
    shutdown = true;
    cVar.notify_all();

    for (auto& thr : workers)
    {
        thr.join();
    }

}

void ResourceLoader::WaitForAllLoads()
{
    foundation::ExponentialBackoffSleeper sleeper(
        std::chrono::milliseconds(1),   // min: 1ms
        std::chrono::milliseconds(100), // max: 100ms
        0.2f,                          // 20% jitter
        1.5f                           // 1.5x backoff multiplier
    );
    
    while (!requests.empty())
    {
        sleeper.sleepAndBackoff();
    }
}

void ResourceLoader::workerFunction()
{
    namespace fs = std::filesystem;

    while (!shutdown)
    {
        queueMutex.lock();
        cVar.wait(queueMutex, [this]()->bool { return shutdown || !requests.empty(); });

        if (requests.empty())
        {
            // get here when shutdown set true: we still want to finish out queued loads though
            queueMutex.unlock();
            return;
        }

        loadRequest request = requests.front();
        requests.pop_front();
        FactoryFunctor factoryFn = factories.at(request.destinationData.FileType);
        queueMutex.unlock();

        if (!fs::exists(request.destinationData.FileName))
        {
            // Gotta find file path.
            std::string foundPath = FindFile(request.destinationData.FileName, request.destinationData.SearchDir, 2);
            if (foundPath.empty())
            {
                std::lock_guard failMutex{ k_LogMutex };
                std::cerr << "Failed to load resource! File name was " << request.destinationData.FileName;
                throw std::runtime_error("Failed to load resource!"); // how could we handle this without throwing?
            }
            request.destinationData.AbsoluteFilePath = std::move(foundPath);
            request.destinationData.SearchDir.clear();
            request.destinationData.SearchDir.shrink_to_fit();

        }

        if (request.type == load_req_type::FreshLoad)
        {

            request.destinationData.Data = factoryFn(request.destinationData.AbsoluteFilePath.c_str(), request.userData);
            auto iter = resources.emplace(request.destinationData.FileNameHash, std::move(request.destinationData));
            void* data = iter.first->second.Data;
            // signal first requester first
            request.signal(request.requester, data, request.userData);
            if (pendingResourceListeners.count(iter.first->first) != 0)
            {
                // we can use the same signal function on resources with same type string
                std::lock_guard pendingDataGuard(pendingDataMutex);
                auto& listenersVec = pendingResourceListeners.at(iter.first->first);
                while (!listenersVec.empty())
                {
                    request.signal(listenersVec.back().first, data, listenersVec.back().second);
                    // we couldn't increment this earlier because it didn't exist, so let's do so now
                    ++iter.first->second.RefCount;
                    listenersVec.pop_back();
                }
                pendingResourceListeners.erase(iter.first->first);
                // lock automatically released when pendingDataLock goes out of scope
            }
            // call other dependent items
            pendingResources.erase(iter.first->first);
        }
        else if (request.type == load_req_type::AlreadyLoaded)
        {
            // just dispatch a signal
            auto& data = resources.at(request.destinationData.FileNameHash);
            request.signal(request.requester, data.Data, request.userData);
            data.RefCount += 1;
        }
    }
}
