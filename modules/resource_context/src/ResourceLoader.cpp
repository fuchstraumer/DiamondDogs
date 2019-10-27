#include "ResourceLoader.hpp"
#ifndef __APPLE_CC__
#include <filesystem>
#else
#include <boost/filesystem.hpp>
#endif
#include "easylogging++.h"
#include <algorithm>
#include <locale>
#include <execution>

static std::mutex logMutex;

ResourceLoader::ResourceLoader()
{
    Start();
}

ResourceLoader::~ResourceLoader()
{
    Stop();
}

void ResourceLoader::Subscribe(const char* file_type, FactoryFunctor func, DeleteFunctor del_fn) {
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
#ifndef __APPLE_CC__
    namespace fs = std::filesystem;
#else
    namespace fs = boost::filesystem;
#endif
    const std::string file_name{ file_path };
    const uint64_t fileNameHash{ std::hash<std::string>()(file_name) };

    {
        // Check to see if resource is already loaded
        std::lock_guard pendingDataGuard(pendingDataMutex);
        if (pendingResources.count(fileNameHash) != 0) {
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
        std::lock_guard pendingResourcesGuard(pendingDataMutex);
        req.type = load_req_type::FreshLoad;
        pendingResources.emplace(fileNameHash);
    }

    {
        std::unique_lock<std::recursive_mutex> guard(queueMutex);
        requests.push_back(req);
        guard.unlock();
    }
    cVar.notify_one();
}

void ResourceLoader::Load(const char* file_type, const char* _file_name, const char* search_dir, void* _requester, SignalFunctor signal, void* user_data)
{
#ifndef __APPLE_CC__
    namespace fs = std::filesystem;
#else
    namespace fs = boost::filesystem;
#endif
    const std::string file_name{ _file_name };
    const uint64_t fileNameHash{ std::hash<std::string>()(file_name) };

    {
        // Check to see if resource is already queued for load
        std::lock_guard pendingDataGuard(pendingDataMutex);
        if (pendingResources.count(fileNameHash) != 0) {
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
        std::lock_guard pendingResourcesGuard(pendingDataMutex);
        req.type = load_req_type::FreshLoad;
        pendingResources.emplace(fileNameHash);
    }

    {
        std::unique_lock<std::recursive_mutex> guard(queueMutex);
        requests.push_back(req);
        guard.unlock();
    }
    cVar.notify_one();
}

void ResourceLoader::Unload(const char* file_type, const char* _path)
{
#ifndef __APPLE_CC__
    namespace fs = std::filesystem;
#else
    namespace fs = boost::filesystem;
#endif

    uint64_t pathHash = std::hash<std::string>()(_path);

    std::lock_guard<std::recursive_mutex> guard(queueMutex);
    if (auto iter = resources.find(pathHash); iter != std::end(resources)) {
        --iter->second.RefCount;
        if (iter->second.RefCount == 0) {
            deleters.at(iter->second.FileType)(iter->second.Data, nullptr);
        }
        resources.erase(iter);
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
    static std::mutex cacheMutex;

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
        std::lock_guard cacheGuard(cacheMutex);
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
            std::lock_guard cacheGuard(cacheMutex);
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
    size_t counter = 0;
    while (!requests.empty())
    {
        // every 100 loops, sleep for a millisecond so we don't just burn this core
        if (counter % 100 == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        ++counter;
    }

    counter = 0;
}

void ResourceLoader::workerFunction()
{
    namespace fs = std::filesystem;

    while (!shutdown)
    {
        std::unique_lock<std::recursive_mutex> lock{queueMutex};
        cVar.wait(lock, [this]()->bool { return shutdown || !requests.empty(); });

        if (requests.empty())
        {
            // get here when shutdown set true: we still want to finish out queued loads though
            return;
        }

        loadRequest request = requests.front();
        requests.pop_front();
        FactoryFunctor factory_fn = factories.at(request.destinationData.FileType);
        lock.unlock();

        if (!fs::exists(request.destinationData.FileName))
        {
            // Gotta find file path.
            std::string found_path = FindFile(request.destinationData.FileName, request.destinationData.SearchDir, 2);
            if (found_path.empty())
            {
                std::lock_guard failMutex{ logMutex };
                LOG(ERROR) << "Failed to load resource! File name was " << request.destinationData.FileName;
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
                std::unique_lock<std::recursive_mutex> pendingDataLock(pendingDataMutex);
                auto& listeners_vec = pendingResourceListeners.at(iter.first->first);
                while (!listeners_vec.empty())
                {
                    request.signal(listeners_vec.back().first, data, listeners_vec.back().second);
                    // we couldn't increment this earlier because it didn't exist, so let's do so now
                    ++iter.first->second.RefCount;
                    listeners_vec.pop_back();
                }
                pendingResourceListeners.erase(iter.first->first);
                // safe to unlock, signal function won't mutate object variables unsafely
                pendingDataLock.unlock();
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

