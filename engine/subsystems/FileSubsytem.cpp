#include "stdafx.h"
#include "FileSubsytem.hpp"
#include <experimental\filesystem>

#ifdef _WIN32
#include <Shlobj.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

FileSubsystem::engine_paths_t EnginePaths = FileSubsystem::engine_paths_t();

void FileSubsystem::SetupRequiredPaths() {

}

void FileSubsystem::CleanShaderCache(const std::string & cache_path_str, const std::vector<uint16_t>& used_cache_ids) {

	auto cache_path = std::experimental::filesystem::path(cache_path_str);
	size_t files_erased = 0;

	if (std::experimental::filesystem::exists(cache_path) && !used_cache_ids.empty()) {
		auto dir_iter = std::experimental::filesystem::directory_iterator(cache_path);
		for (auto& p : dir_iter) {

			bool id_used = false;

			if (p.path().extension().string() == ".vkdat") {

				std::string cache_name = p.path().filename().string();
				size_t idx = cache_name.find_last_of('.');
				cache_name = cache_name.substr(0, idx);

				uint16_t id = static_cast<uint16_t>(std::stoi(cache_name));

				for (const uint16_t& curr : used_cache_ids) {
					if (curr == id) {
						id_used = true;
						continue;
					}
				}

			}

			if (!id_used) {
				bool erased = std::experimental::filesystem::remove(p.path());
				if (!erased) {
					LOG(WARNING) << "Failed to erase a pipeline cache with ID " << p.path().filename().string();
				}
				else {
					++files_erased;
				}
			}
		}
	}

	if (files_erased != 0) {
		LOG(INFO) << "Erased " << std::to_string(files_erased) << " unused pipeline/shader cache files.";
	}

}

void FileSubsystem::setShaderCachePath() {
	namespace fs = std::experimental::filesystem;

	auto sc_path = fs::path(".tmp/shader_cache");

	if (!fs::exists(sc_path)) {
		bool created = fs::create_directory(sc_path);
		if (!created) {
			LOG(ERROR) << "Failed to create shader cache directory! Files won't save properly, but program will still run."; 
		}
	}

	EnginePaths.ShaderCachePath = sc_path.relative_path().string();

}

void FileSubsystem::setResourcePath() {
	namespace fs = std::experimental::filesystem;

	auto rsrc_path = fs::path("./rsrc");

	if (!fs::exists(rsrc_path)) {
		LOG(ERROR) << "Resource path not found! Re-install the program.";
		throw std::runtime_error("Could not locate resource files directory.");
	}
	else {
		EnginePaths.ResourcePath = rsrc_path.relative_path().string();
	}
}

void FileSubsystem::setUserPath() {

	namespace fs = std::experimental::filesystem;
	std::string path_str;

#ifdef _WIN32
	WCHAR path[MAX_PATH];
	if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, 0, path))) {
		int size_needed = WideCharToMultiByte(CP_UTF8, 0, path, MAX_PATH, NULL, 0, NULL, NULL);
		std::string buff(static_cast<size_t>(size_needed), 0);
		WideCharToMultiByte(CP_UTF8, 0, path, MAX_PATH, buff.data(), size_needed, NULL, NULL);
		path_str = buff;
	}
	else {
		LOG(ERROR) << "Failed to find user's documents folder!";
		throw std::runtime_error("Couldn't find user's documents folder.");
	}
	path_str += "\\DiamondDogs";
#elif defined(__linux__)
	const char* home_dir;
	if ((home_dir = getenv("XDG_CONFIG_HOME")) == nullptr) {
		if ((home_dir = getenv("HOME")) == nullptr) {
			homedir = getpwuid(getuid())->pw_dir;
		}
	}
	
	if (homedir == nullptr) {
		LOG(ERROR) << "Failed to find user's home folder!";
		throw std::runtime_error("Couldn't find user's home folder.");
	}

	path_str = homedir;
	path_str += "/DiamondDogs";
#endif

	fs::path user_path(path_str);

	if (!fs::exists(user_path)) {
		bool created = fs::create_directory(user_path);
		if (!created) {
			LOG(ERROR) << "Failed to create user directory!";
			throw std::runtime_error("Failed to create user directory.");
		}
	}
	
	// use system complete to make sure OS compatability is complete.
	EnginePaths.UserPath = fs::system_complete(user_path).string();

}

void FileSubsystem::setLogPath() {

	namespace fs = std::experimental::filesystem;

	fs::path log_path(".tmp/logs");

	if (!fs::exists(log_path)) {
		bool created = fs::create_directory(log_path);
		if (!created) {
			LOG(ERROR) << "Failed to create log path.";
			throw std::runtime_error("Failed to create log path.");
		}
	}

	EnginePaths.LogPath = log_path.relative_path().string();

}
