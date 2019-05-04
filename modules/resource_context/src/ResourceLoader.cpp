#include "ResourceLoader.hpp"
#ifndef __APPLE_CC__
#include <experimental/filesystem>
#else
#include <boost/filesystem.hpp>
#endif
#include "easylogging++.h"

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
    namespace fs = std::experimental::filesystem;
#else
    namespace fs = boost::filesystem;
#endif
    fs::path fs_file_path = fs::absolute(fs::path(file_path));
    const std::string absolute_path = fs_file_path.string();

    {
        // Check to see if resource is already loaded
        std::lock_guard pendingDataGuard(pendingDataMutex);
        if (pendingResources.count(absolute_path) != 0) {
            auto& listeners_vec = pendingResourceListeners[absolute_path];
            listeners_vec.emplace_back(_requester, user_data);
            return;
        }       
    }
        
    if (!fs::exists(fs_file_path))
    {
        throw std::runtime_error("Given path to a resource does not exist.");
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
    pendingDataMutex.lock();
    pendingResources.emplace(absolute_path);
    pendingDataMutex.unlock();
    data.AbsoluteFilePath = absolute_path;
    data.RefCount = 1;

    loadRequest req(data);
    req.requester = _requester;
    req.signal = signal;
    req.userData = user_data;
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
    namespace fs = std::experimental::filesystem;
#else
    namespace fs = boost::filesystem;
#endif
    fs::path file_path(_path);
    if (!fs::exists(file_path)) {
        LOG(ERROR) << "Tried to unload non-existent file path!";
    }
        
    const std::string path = fs::absolute(file_path).string();

    if (resources.count(path) != 0) {
        std::lock_guard<std::recursive_mutex> guard(queueMutex);
        auto iter = resources.find(path);
        --iter->second.RefCount;
        if (iter->second.RefCount == 0) {
            deleters.at(iter->second.FileType)(iter->second.Data, nullptr);
        }
        resources.erase(iter);
    }
}

ResourceLoader & ResourceLoader::GetResourceLoader()
{
    static ResourceLoader loader;
    return loader;
}

void ResourceLoader::Start()
{
    shutdown = false;

    for (auto& thr : workers)
    {
        thr = std::thread(&ResourceLoader::workerFunction, this);
    }

}

void ResourceLoader::Stop()
{
    shutdown = true;      
        
    cVar.notify_all();

    for (auto& thr : workers)
    {
        if (thr.joinable())
        {
            thr.join();
        }
    }

    while (!resources.empty())
    {
        auto iter = resources.begin();
        Unload(iter->second.FileType.c_str(), iter->first.c_str());
    }

}

void ResourceLoader::workerFunction()
{
    while (!shutdown)
    {
        std::unique_lock<std::recursive_mutex> lock{queueMutex};
        cVar.wait(lock, [this]()->bool { return shutdown || !requests.empty(); });

        if (shutdown)
        {
            lock.unlock();
            return;
        }

        loadRequest request = requests.front();
        requests.pop_front();
        FactoryFunctor factory_fn = factories.at(request.destinationData.FileType);
        lock.unlock(); 

        {
            request.destinationData.Data = factory_fn(request.destinationData.AbsoluteFilePath.c_str(), request.userData);
            auto iter = resources.emplace(request.destinationData.AbsoluteFilePath, std::move(request.destinationData));
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
    }
}

void ResourceLoader::waitForPendingRequest(const std::string & absolute_file_path, SignalFunctor signal) {

}

