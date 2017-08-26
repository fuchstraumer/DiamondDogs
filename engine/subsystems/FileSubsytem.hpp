#ifndef DIAMOND_DOGS_FILE_SUBSYSTEM_H
#define DIAMOND_DOGS_FILE_SUBSYSTEM_H

#include "stdafx.h"


class FileSubsystem {

	FileSubsystem(const FileSubsystem&) = delete;
	FileSubsystem& operator=(const FileSubsystem&) = delete;

public:

	static void SetupRequiredPaths();

	static void CleanShaderCache(const std::string& cache_path_str, const std::vector<uint16_t>& used_cache_ids);

	static void SaveConfiguration();
	static void LoadConfiguration(const std::string& filename);

	struct engine_paths_t {
		std::string ShaderCachePath;
		std::string ResourcePath;
		std::string UserPath;
		std::string LogPath;
	};

	static engine_paths_t EnginePaths;

private:

	static void setShaderCachePath();
	static void setResourcePath();
	static void setUserPath();
	static void setLogPath();

};

#endif // !DIAMOND_DOGS_FILE_SUBSYSTEM_H
