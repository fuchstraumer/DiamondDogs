
add_module(task_graph
    "include/CoroutineAllocator.hpp"
    "include/IScheduler.hpp"
    "include/Task.hpp"
    "include/UnifiedTimelineSemaphore.hpp"
    "src/CoroutineAllocator.cpp"
)

TARGET_INCLUDE_DIRECTORIES(task_graph PUBLIC 
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "../../third_party/mimalloc/include"
)