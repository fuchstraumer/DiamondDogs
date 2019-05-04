#pragma once
#ifndef VPSK_RESOURCE_LOADER_HPP
#define VPSK_RESOURCE_LOADER_HPP
#include <array>
#include <memory>
#include <thread>
#include <condition_variable>
#include <future>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <vector>

using FactoryFunctor = void*(*)(const char* fname, void* user_data);
using DeleteFunctor = void(*)(void* obj_instance, void* user_data);
using SignalFunctor = void(*)(void* state, void* data, void* user_data);

class ResourceLoader {
    ResourceLoader(const ResourceLoader&) = delete;
    ResourceLoader& operator=(const ResourceLoader&) = delete;
    ResourceLoader();
    ~ResourceLoader();
public:

    void Subscribe(const char* file_type, FactoryFunctor func, DeleteFunctor del_fn);
    void Unsubscribe(const char* file_type);
    void Load(const char* file_type, const char* file_path, void* requester, SignalFunctor signal, void* user_data = nullptr);
    void Unload(const char* file_type, const char* path);

    void Start();
    void Stop();

    static ResourceLoader& GetResourceLoader();

    struct ResourceData {
        void* Data;
        std::string FileType;
        std::string AbsoluteFilePath;
        size_t RefCount{ 0 };
    };

private:

    struct loadRequest {
        loadRequest(ResourceData dest) : destinationData(dest), requester(nullptr) {}
        ResourceData destinationData;
        void* requester; // state pointer of requesting object, if given
        void* userData; // may contain additional parameters passed to factory function
        SignalFunctor signal;
    };

    void workerFunction();
    void waitForPendingRequest(const std::string& absolute_file_path, SignalFunctor signal);

    std::unordered_map<std::string, FactoryFunctor> factories;
    std::unordered_map<std::string, DeleteFunctor> deleters;
    std::unordered_map<std::string, ResourceData> resources;
    std::unordered_set<std::string> pendingResources;
    std::unordered_map<std::string, std::vector<std::pair<void*, void*>>> pendingResourceListeners;
    std::list<loadRequest> requests;
    std::recursive_mutex queueMutex;
    std::recursive_mutex pendingDataMutex;
    std::mutex subscribeMutex;
    std::condition_variable_any cVar;
    std::atomic<bool> shutdown{ false };
    std::array<std::thread, 2> workers;
};

#endif //!VPSK_RESOURCE_LOADER_HPP
