#include "ResourceLoader.hpp"
#ifndef __APPLE_CC__
#include <experimental/filesystem>
#else
#include <boost/filesystem.hpp>
#endif
#include "easylogging++.h"

ResourceLoader::ResourceLoader() {
    Start();
}

ResourceLoader::~ResourceLoader() {
    Stop();
}

void ResourceLoader::Subscribe(const char* file_type, FactoryFunctor func, DeleteFunctor del_fn) {
    factories[file_type] = func;
    deleters[file_type] = del_fn;
}

void ResourceLoader::Load(const char* file_type, const char* file_path, void* _requester, SignalFunctor signal, void* user_data) {
#ifndef __APPLE_CC__
    namespace fs = std::experimental::filesystem;
#else
    namespace fs = boost::filesystem;
#endif
    fs::path fs_file_path = fs::absolute(fs::path(file_path));
    const std::string absolute_path = fs_file_path.string();

    {
        // Check to see if resource is already loaded. Don't bother with edge case of a request
        // already in pending_resources
        if (resources.count(absolute_path) != 0) {
            ++resources.at(absolute_path).RefCount;
            signal(_requester, resources.at(absolute_path).Data);
            return;
        }       
    }
        
    if (!fs::exists(fs_file_path)) {
        throw std::runtime_error("Given path to a resource does not exist.");
    }

    if (factories.count(file_type) == 0) {
        throw std::domain_error("Tried to load resource type for which there is no factory!");
    }

    if (deleters.count(file_type) == 0) {
        throw std::domain_error("No deleter function for current file type!");
    }

    ResourceData data;      
    data.FileType = file_type;
    pendingResources.emplace(absolute_path);
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

void ResourceLoader::Unload(const char* file_type, const char* _path) {
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
            deleters.at(iter->second.FileType)(iter->second.Data);
        }
        resources.erase(iter);
    }
}

ResourceLoader & ResourceLoader::GetResourceLoader() {
    static ResourceLoader loader;
    return loader;
}

void ResourceLoader::Start() {
    shutdown = false;
    workers[0] = std::thread(&ResourceLoader::workerFunction, this);
    workers[1] = std::thread(&ResourceLoader::workerFunction, this);
}

void ResourceLoader::Stop() {
    shutdown = true;      
        
    cVar.notify_all();

    if (workers[0].joinable()) {
        workers[0].join();
    }
        
    if (workers[1].joinable()) {
        workers[1].join();
    }

    while (!resources.empty()) {
        auto iter = resources.begin();
        Unload(iter->second.FileType.c_str(), iter->first.c_str());
    }

}

void ResourceLoader::workerFunction() {
    while (!shutdown) {
        std::unique_lock<std::recursive_mutex> lock{queueMutex};
        cVar.wait(lock, [this]()->bool { return shutdown || !requests.empty(); });

        if (shutdown) {
            lock.unlock();
            return;
        }

        loadRequest request = requests.front();
        requests.pop_front();
        FactoryFunctor factory_fn = factories.at(request.destinationData.FileType);
        lock.unlock(); 

        {
            request.destinationData.Data = factory_fn(request.destinationData.AbsoluteFilePath.c_str(), request.userData);
            std::unique_lock<std::recursive_mutex> pendingDataLock(pendingDataMutex);
            // how to handle failure to emplace? what could it mean?
            auto iter = resources.emplace(request.destinationData.AbsoluteFilePath, std::move(request.destinationData));
            pendingResources.erase(request.destinationData.AbsoluteFilePath);
            void* data = iter.first->second.Data;
            // safe to unlock, signal function won't mutate object variables unsafely
            pendingDataLock.unlock();
            request.signal(request.requester, data);
        }
    }
}

void ResourceLoader::waitForPendingRequest(const std::string & absolute_file_path, SignalFunctor signal) {

}

