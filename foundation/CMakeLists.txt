
set(foundation_source_files
    "include/ImageDataFormats.hpp"
    "include/ImageDataFormatsVulkan.hpp"
    "include/Unit.hpp"
    "include/Handle.hpp"
    "src/ImageDataFormats.cpp"
    # Replaces global new/delete with mimalloc in every executable that links foundation
    "src/miMallocOverride.cpp")

set(foundation_async_source_files
    "include/async/FrameHeap.hpp"
    "include/async/FrameNames.hpp"
    "include/async/FrameStatistics.hpp"
    "include/async/Promise.hpp"
    "include/async/Scheduler.hpp"
    "include/async/Task.hpp"
    "include/async/Timeline.hpp"
    "include/async/TimelineSource.hpp"
    "include/async/Yield.hpp"
    "src/async/FrameHeap.cpp"
    "src/async/FrameNames.cpp"
    "src/async/Promise.cpp"
    "src/async/Scheduler.cpp"
    "src/async/Timeline.cpp"
    "src/async/Yield.cpp")

set(foundation_content_source_files
    "include/content/ByteStreamSink.hpp"
    "include/content/BytesSink.hpp"
    "include/content/CanonicalSink.hpp"
    "include/content/Encode.hpp"
    "include/content/EncodeErased.hpp"
    "include/content/HashSink.hpp"
    "include/content/JsonSink.hpp"
    "include/content/JsonWriter.hpp"
    "include/content/Key.hpp"
    "include/content/RecipeKey.hpp"
    "include/content/TextSink.hpp"
    "src/content/ByteStreamSink.cpp"
    "src/content/BytesSink.cpp"
    "src/content/EncodeErased.cpp"
    "src/content/HashSink.cpp"
    "src/content/JsonSink.cpp"
    "src/content/JsonWriter.cpp"
    "src/content/Key.cpp"
    "src/content/RecipeKey.cpp"
    "src/content/TextSink.cpp")

set(foundation_net_source_files
    "include/net/Socket.hpp"
    "src/net/SocketWaker.cpp"
    "src/net/SocketWin32.cpp")

set(foundation_reflect_source_files
    "include/reflect/Describe.hpp"
    "include/reflect/TypeInfo.hpp"
    "include/reflect/ValueAccess.hpp"
    "include/reflect/ValueTraits.hpp"
    "src/reflect/TypeInfo.cpp"
    "src/reflect/ValueAccess.cpp")

set(foundation_containers_source_files
    "include/containers/CircularBuffer.hpp"
    "include/containers/ConcurrentDeque.hpp"
    "include/containers/DenseHashMap.hpp"
    "include/containers/LruCache.hpp"
    "include/containers/mwsrQueue.hpp"
    "include/containers/SmallVector.hpp")

set(threading_source_files
    "include/threading/atomic128.hpp"
    "include/threading/concurrent_vector.hpp"
    "include/threading/CriticalSection.hpp"
    "include/threading/ExponentialBackoffSleeper.hpp"
    "include/threading/FrameLimiter.hpp"
    "include/threading/PrecisionTimer.hpp"
    "include/threading/SrwLock.hpp"
    "src/threading/atomic128.cpp"
    "src/threading/ExponentialBackoffSleeper.cpp"
    "src/threading/FrameLimiter.cpp")

set(event_source_files
    "include/events/DisplayEvents.hpp"
    "include/events/PlatformEvents.hpp"
    "include/events/RhiEvents.hpp")

if(WIN32)
    set(threading_platform_source_files
        "src/threading/win32/CriticalSectionWin32.cpp"
        "src/threading/win32/PrecisionTimerWin32.cpp"
        "src/threading/win32/SrwLockWin32.cpp")
elseif(UNIX)
    set(threading_platform_source_files
        "src/threading/unix/CriticalSectionUnix.cpp"
        "src/threading/unix/PrecisionTimerUnix.cpp"
        "src/threading/unix/SrwLockUnix.cpp")
endif()

set(utility_source_files
    "include/utility/Delegate.hpp"
    "include/utility/EnumFlagUtils.hpp"
    "include/utility/MulticastDelegate.hpp"
    "include/utility/MurmurHash.hpp"
    "include/utility/TaggedBool.hpp"
    "src/utility/MurmurHash.cpp")

set(reactors_source_files
    "include/reactors/casReactor.hpp"
    "include/reactors/AllocationReactor.hpp"
    "src/reactors/AllocationReactor.cpp")


if(NOT EMSCRIPTEN)
    set(math_backend_source_files
        "include/math/MathBackendDX.inl"
        "include/math/QuaternionBackendDX.inl"
        "include/math/TranscendentalBackendDX.inl")
elseif(EMSCRIPTEN)
    set(math_backend_source_files
        "include/math/MathBackendWASM.inl"
        "include/math/QuaternionBackendWASM.inl"
        "include/math/TranscendentalBackendWASM.inl")
else()
    message(FATAL_ERROR "Unsupported platform for math backend source files")
endif()

set(maths_source_files
    "include/math/Math.hpp"
    "include/math/Math.inl"
    "include/math/MathHashes.hpp"
    "include/math/MathPolynomials.hpp"
    ${math_backend_source_files})

source_group("foundation" FILES ${foundation_source_files})
source_group("async" FILES ${foundation_async_source_files})
source_group("content" FILES ${foundation_content_source_files})
source_group("net" FILES ${foundation_net_source_files})
source_group("reflect" FILES ${foundation_reflect_source_files})
source_group("containers" FILES ${foundation_containers_source_files})
source_group("threading" FILES ${threading_source_files} ${threading_platform_source_files})
source_group("utility" FILES ${utility_source_files})
source_group("reactors" FILES ${reactors_source_files})
source_group("maths" FILES ${maths_source_files})
source_group("events" FILES ${event_source_files})

add_library(foundation STATIC
    ${foundation_async_source_files}
    ${foundation_content_source_files}
    ${foundation_net_source_files}
    ${foundation_reflect_source_files}
    ${foundation_source_files}
    ${foundation_containers_source_files}
    ${threading_source_files}
    ${event_source_files}
    ${threading_platform_source_files}
    ${utility_source_files}
    ${reactors_source_files}
    ${maths_source_files})

target_include_directories(foundation PRIVATE
    "../third_party"
    ${Vulkan_INCLUDE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/include/")

target_link_libraries(foundation PUBLIC mimalloc-static)

# xxHash comes from Lodestone's checkout (see cmake/ConfigureLodestone.cmake). PRIVATE, so no header of ours shows it.
target_link_libraries(foundation PRIVATE xxHash::xxhash)

if(WIN32)
    # FrameSymbolizer reads function names with DbgHelp in every build below Release (RelWithDebInfo defines NDEBUG too,
    # so NDEBUG can't decide this)
    target_link_libraries(foundation PUBLIC dbghelp winmm ws2_32)
    target_compile_definitions(foundation PUBLIC "FOUNDATION_ASYNC_SYMBOLS=$<IF:$<CONFIG:Release,MinSizeRel>,0,1>")
endif()

# The frame counters cost atomics on the allocation path, so Release and MinSizeRel compile them out. NDEBUG can't decide
# this: RelWithDebInfo defines it and keeps the counters
target_compile_definitions(foundation PUBLIC "FOUNDATION_ASYNC_FRAME_STATISTICS=$<IF:$<CONFIG:Release,MinSizeRel>,0,1>")

if(UNIX)
    target_link_libraries(foundation PUBLIC "c++fs" "dl" "pthread")
endif()

if(MSVC)
    # make sure DirectXMath has right arch defines enabled
    target_compile_definitions(foundation PUBLIC "NOMINMAX" "XM_AVX2_INTRINSICS_")
    # SVML is a library that only cl.exe provides
    if(DIAMOND_DOGS_COMPILER_IS_CL)
        target_compile_definitions(foundation PUBLIC "_XM_SVML_INTRINSICS_")
        target_compile_options(foundation PRIVATE $<$<CONFIG:RELEASE>:/MP> $<$<CONFIG:RELWITHDEBINFO>:/MP>)
    endif()
    # Enable intrinsics in both release and relwithdebinfo, since those shouldn't have much debug info anyways
    # (And can largely affect perf)
    target_compile_options(foundation PRIVATE $<$<CONFIG:RELEASE>:/Oi> $<$<CONFIG:RELWITHDEBINFO>:/Oi>)
    # Set standard to latest, enable coroutines, and ensure AVX2 is 
    set_target_properties(foundation PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
    target_compile_options(foundation PUBLIC "/arch:AVX2" "/volatile:iso")
else()
    # Set standard to C++20
    set_target_properties(foundation PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED YES)
endif()

# Put foundation in the Modules folder in the IDE, avoiding root clutter (and it is just the root module, really)
set_target_properties(foundation PROPERTIES FOLDER "Modules")

diamond_dogs_add_pch(foundation LIBRARY)