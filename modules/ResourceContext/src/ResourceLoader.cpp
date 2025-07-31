#include "ResourceLoader.hpp"
#include <filesystem>
#include <algorithm>
#include <locale>
#include <execution>
#include <iostream>
#include "threading/ExponentialBackoffSleeper.hpp"
#include "threading/CriticalSection.hpp"

static std::mutex logMutex;

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
    const std::string file_name{ file_path };
    const uint64_t fileNameHash{ std::hash<std::string>()(file_name) };

    {
        // Check to see if resource is already loaded
        auto pendingDataGuard = pendingDataMutex.GetLock();
        if (pendingResources.count(fileNameHash) != 0)
        {
            auto& listeners_vec = pendingResourceListeners[fileNameHash];
            listeners_vec.emplace_back(_requester, user_data);
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
    data.FileName = file_name;
    data.FileNameHash = fileNameHash;

    if (fs::exists(file_name))
    {
        data.AbsoluteFilePath = fs::canonical(file_name).string();
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
        auto pendingResourcesGuard = pendingDataMutex.GetLock();
        req.type = load_req_type::FreshLoad;
        pendingResources.emplace(fileNameHash);
    }

    {
        auto guard = queueMutex.GetLock();
        requests.push_back(req);
    }
    cVar.notify_one();
}

void ResourceLoader::Load(const char* file_type, const char* _file_name, const char* search_dir, void* _requester, SignalFunctor signal, void* user_data)
{
    namespace fs = std::filesystem;
    const std::string file_name{ _file_name };
    const uint64_t fileNameHash{ std::hash<std::string>()(file_name) };

    {
        // Check to see if resource is already queued for load
        auto pendingDataGuard = pendingDataMutex.GetLock();
        if (pendingResources.count(fileNameHash) != 0)
        {
            auto& listeners_vec = pendingResourceListeners[fileNameHash];
            listeners_vec.emplace_back(_requester, user_data);
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
    data.FileName = file_name;
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
        auto pendingResourcesGuard = pendingDataMutex.GetLock();
        req.type = load_req_type::FreshLoad;
        pendingResources.emplace(fileNameHash);
    }

    {
        auto guard = queueMutex.GetLock();
        requests.push_back(req);
    }
    cVar.notify_one();
}

void ResourceLoader::Unload(const char* file_type, const char* _path)
{
    namespace fs = std::filesystem;

    uint64_t pathHash = std::hash<std::string>()(_path);

    auto guard = queueMutex.GetLock();
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

    auto case_insensitive_comparison = [](const std::string & fname, const std::string & curr_entry)->bool
    {

        return std::equal(std::execution::par_unseq, fname.cbegin(), fname.cend(), curr_entry.cbegin(), curr_entry.cend(), [](const char a, const char b)
            {
                return std::tolower(a) == std::tolower(b);
            });
    };

    stdfs::path starting_path(stdfs::canonical(init_dir));

    stdfs::path file_name_path(fname);
    file_name_path = file_name_path.filename();

    if (stdfs::exists(file_name_path))
    {
        return file_name_path.string();
    }

    {
        auto cacheGuard = cacheCS.GetLock();
        auto iter = foundPathsCache.find(fname);
        if (iter != foundPathsCache.end())
        {
            return iter->second.string();
        }
    }

    stdfs::path file_path = starting_path;

    for (size_t i = 0; i < depth; ++i)
    {
        file_path = file_path.parent_path();
    }

    for (auto& dir_entry : stdfs::recursive_directory_iterator(file_path))
    {
        if (dir_entry == starting_path)
        {
            continue;
        }

        if (!stdfs::is_regular_file(dir_entry) || stdfs::is_directory(dir_entry))
        {
            continue;
        }

        const stdfs::path entry_path(dir_entry);
        const std::string curr_entry_str = entry_path.filename().string();

        if (case_insensitive_comparison(fname, curr_entry_str))
        {
            auto cacheGuard = cacheCS.GetLock();
            auto iter = foundPathsCache.emplace(fname, entry_path);
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
        FactoryFunctor factory_fn = factories.at(request.destinationData.FileType);
        queueMutex.unlock();

        if (!fs::exists(request.destinationData.FileName))
        {
            // Gotta find file path.
            std::string found_path = FindFile(request.destinationData.FileName, request.destinationData.SearchDir, 2);
            if (found_path.empty())
            {
                std::lock_guard failMutex{ logMutex };
                std::cerr << "Failed to load resource! File name was " << request.destinationData.FileName;
                throw std::runtime_error("Failed to load resource!"); // how could we handle this without throwing?
            }
            request.destinationData.AbsoluteFilePath = std::move(found_path);
            request.destinationData.SearchDir.clear();
            request.destinationData.SearchDir.shrink_to_fit();

        }

        if (request.type == load_req_type::FreshLoad)
        {

            request.destinationData.Data = factory_fn(request.destinationData.AbsoluteFilePath.c_str(), request.userData);
            auto iter = resources.emplace(request.destinationData.FileNameHash, std::move(request.destinationData));
            void* data = iter.first->second.Data;
            // signal first requester first
            request.signal(request.requester, data, request.userData);
            if (pendingResourceListeners.count(iter.first->first) != 0)
            {
                // we can use the same signal function on resources with same type string
                auto pendingDataLock = pendingDataMutex.GetLock();
                auto& listeners_vec = pendingResourceListeners.at(iter.first->first);
                while (!listeners_vec.empty())
                {
                    request.signal(listeners_vec.back().first, data, listeners_vec.back().second);
                    // we couldn't increment this earlier because it didn't exist, so let's do so now
                    ++iter.first->second.RefCount;
                    listeners_vec.pop_back();
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

void ResourceLoader::waitForPendingRequest(const std::string & absolute_file_path, SignalFunctor signal) {

}

