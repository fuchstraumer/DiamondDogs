
if(WIN32)
    # We also need PDB repair utilities on windows to make sure plugin PDBs work right
    set(foundation_plugin_manager_sources 
        "${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/Win32/PluginManagerImpl.hpp" 
        "${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/Win32/PluginManagerImpl.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/Win32/PDB_Helpers.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/Win32/PDB_Helpers.cpp"
    )
    set(plugin_manager_impl_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/Win32")
elseif(UNIX)
    set(foundation_plugin_manager_sources
        "${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/Unix/PluginManagerImpl.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/Unix/PluginManagerImpl.cpp"
    )
    set(plugin_manager_impl_include_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/foundation/Unix")
endif()

set(foundation_source_files
    "include/foundation/CoreAPIs.hpp"
    "include/foundation/Handle.hpp"
    "include/foundation/PluginAPI.hpp"
    "include/foundation/PluginManager.hpp"
    "src/foundation/PluginManager.cpp"
    ${foundation_plugin_manager_sources})

set(foundation_containers_source_files
    "include/containers/circular_buffer.hpp"
    "include/containers/mwsrQueue.hpp")

set(threading_source_files
    "include/threading/atomic128.hpp"
    "include/threading/concurrent_vector.hpp"
    "include/threading/critical_section.hpp"
    "include/threading/mcas.hpp"
    "include/threading/srw_lock.hpp"
    "src/threading/critical_section_win32.cpp"
    "src/threading/srw_lock_win32.cpp")

set(utility_source_files
    "include/utility/delegate.hpp"
    "include/utility/multicast_delegate.hpp"
    "include/utility/MurmurHash.hpp"
    "include/utility/tagged_bool.hpp"
    "src/utility/MurmurHash.cpp")

set(reactors_source_files
    "reactors/include/casReactor.hpp"
    "reactors/include/AllocationReactor.hpp"
    "reactors/src/AllocationReactor.cpp")

source_group("foundation" FILES ${foundation_source_files})
source_group("containers" FILES ${foundation_containers_source_files})
source_group("threading" FILES ${threading_source_files})
source_group("utility" FILES ${utility_source_files})
source_group("reactors" FILES ${reactors_source_files})

add_library(foundation STATIC
    ${foundation_source_files}
    ${foundation_containers_source_files}
    ${threading_source_files}
    ${utility_source_files}
    ${reactors_source_files})

# glfw and easyloggingpp both required for baseline functionality, same with Vulkan. Mango
# isn't used a lot yet, but has useful utility stuff that will save me some time
target_link_libraries(foundation PRIVATE easyloggingpp mango snmalloc_lib mimalloc-static)

set(foundation_vpr_include_dirs
    "../third_party/vulpesrender/alloc/include"
    "../third_party/vulpesrender/command/include"
    "../third_party/vulpesrender/common"
    "../third_party/vulpesrender/core/include"
    "../third_party/vulpesrender/render/include"
    "../third_party/vulpesrender/resource/include"
    "../third_party/vulpesrender/sync/include")

target_include_directories(foundation PRIVATE
    "./reactors/include"
    "../modules/common"
    ${foundation_vpr_include_dirs}
    "../third_party"
    "../third_party/glm"
    "../third_party/mango/include"
    ${Vulkan_INCLUDE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    ${plugin_manager_impl_include_dir})

if(UNIX)
    target_link_libraries(foundation PUBLIC "c++fs" "dl" "pthread")
endif()

if(MSVC)
    # Multithreaded compile, and intrinsic functions
    target_compile_definitions(foundation PUBLIC "NOMINMAX")
    target_compile_options(foundation PRIVATE $<$<CONFIG:RELEASE>:/MP> $<$<CONFIG:RELWITHDEBINFO>:/MP>)
    # Enable intrinsics in both release and relwithdebinfo, since those shouldn't have much debug info anyways
    # (And can largely affect perf)
    target_compile_options(foundation PRIVATE $<$<CONFIG:RELEASE>:/Oi> $<$<CONFIG:RELWITHDEBINFO>:/Oi>)
    # Set standard to latest, enable coroutines, and ensure AVX2 is enabled
    target_compile_options(foundation PUBLIC "/std:c++latest" "/arch:AVX2")
else()
    # Set standard to C++20
    set_target_properties(diamond_dogs_core PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED YES)
endif()
