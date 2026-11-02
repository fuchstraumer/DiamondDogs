ADD_MODULE(input_system
    "include/InputFrame.hpp"
    "include/InputLog.hpp"
    "include/InputTypes.hpp"
    "src/ControlNameTable.hpp"
    "src/InputFrame.cpp"
    "src/InputLog.cpp"
    "src/InputTypes.cpp")

target_include_directories(input_system PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
