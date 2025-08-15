# DiamondDogs Unit Testing Framework

This document describes the unit testing setup for the DiamondDogs graphics engine using Google Test (GTest) and Google Mock (GMock).

## Overview

The testing framework is designed to thoroughly test all modules and systems in the DiamondDogs engine, with special focus on:

- **Foundation Module**: Core threading primitives, lockless data structures, containers, utilities
- **Rendering Context Module**: Vulkan integration, command buffer management, render passes  
- **Resource Context Module**: Resource loading, caching, memory management
- **Concurrency & Thread Safety**: Stress testing of lockless systems and multi-threaded components

## Framework Features

### Google Test Integration
- **Framework**: Google Test (GTest) + Google Mock (GMock)
- **IDE Integration**: Full Visual Studio and VS Code support
- **Test Discovery**: Automatic test discovery and execution
- **Parallel Execution**: Tests can run in parallel for faster feedback
- **XML Reporting**: JUnit-compatible XML output for CI/CD integration

### Testing Categories

#### 1. Foundation Module Tests (`tests/unit_tests/foundation/`)
- **Threading Tests**: 
  - `atomic128.hpp` - 128-bit atomic operations with ABA protection
  - `concurrent_vector.hpp` - Lock-free vector implementation
  - `mcas.hpp` - Multi-word Compare-And-Swap operations
  - `CriticalSection.hpp` - Platform-specific critical sections
  - `srw_lock.hpp` - Slim Reader-Writer locks
  - `ExponentialBackoffSleeper.hpp` - Adaptive sleep for spin-wait loops

- **Container Tests**:
  - `circular_buffer.hpp` - Lock-free circular buffer
  - `mwsrQueue.hpp` - Multiple Writer Single Reader queue

- **Utility Tests**:
  - `delegate.hpp` - Function delegates
  - `multicast_delegate.hpp` - Multi-cast delegates  
  - `MurmurHash.hpp` - Hash function implementation
  - `tagged_bool.hpp` - Type-safe boolean wrapper

#### 2. Rendering Context Tests (`tests/unit_tests/rendering_context/`)
- Vulkan device initialization and management
- Command buffer allocation and thread safety
- Render pass creation and compatibility
- Swapchain management
- Concurrent rendering operations

#### 3. Resource Context Tests (`tests/unit_tests/resource_context/`)
- Resource loading with `ResourceLoader.hpp`
- Resource caching strategies
- Handle management and validation
- Concurrent resource access patterns
- Memory leak detection

## Concurrency & Thread Safety Testing

### Stress Testing Features
The framework includes comprehensive stress tests for lockless systems:

1. **High Contention Tests**: Multiple threads competing for shared resources
2. **ABA Problem Prevention**: Verification that lockless structures handle ABA scenarios
3. **Memory Ordering Tests**: Validation of memory ordering guarantees
4. **Performance Benchmarks**: Comparative performance analysis
5. **Race Condition Detection**: Detection of data races and memory corruption

### Thread Safety Patterns Tested
- Lock-free data structures (atomic operations, CAS loops)
- Reader-writer locks with multiple readers
- Producer-consumer patterns
- Memory pool management
- Resource reference counting

## Fuzzing and Advanced Testing

While Google Test doesn't include built-in fuzzing, the framework supports:

### Property-Based Testing
- Randomized inputs for stress testing
- Edge case generation for boundary conditions
- Statistical validation of concurrent operations

### Additional Tools for Lockless Systems
For more advanced testing of your lockless systems, consider integrating:

1. **Microsoft Coyote** - Systematic testing of concurrent C# code (for reference implementations)
2. **Intel Thread Checker** - Runtime detection of threading errors
3. **Helgrind (Valgrind)** - Race condition detection (Linux)
4. **ThreadSanitizer** - Google's thread safety analyzer
5. **Libfuzzer** - Coverage-guided fuzzing for C++

## Building and Running Tests

### Prerequisites
- CMake 3.10+
- Visual Studio 2019+ or compatible compiler
- C++23 support

### Build Configuration
```bash
# Configure with tests enabled
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# Build all tests
cmake --build build --config Debug --target foundation_tests rendering_context_tests resource_context_tests

# Run all tests with CTest
ctest --test-dir build --output-on-failure --verbose
```

### VS Code Integration

#### Required Extensions
1. **C++ TestMate** - Test discovery and execution
2. **CMake Tools** - CMake integration
3. **C/C++ Extension Pack** - Core C++ support

#### Running Tests in VS Code
1. **Command Palette**: `Ctrl+Shift+P` → "Test: Run All Tests"
2. **Test Explorer**: View → Test → Test Explorer
3. **Keyboard Shortcuts**: 
   - `Ctrl+;` → Run test at cursor
   - `Ctrl+Shift+;` → Debug test at cursor

#### VS Code Tasks
- `Ctrl+Shift+P` → "Tasks: Run Task"
  - "Build All Tests"
  - "Run Foundation Tests" 
  - "Run All Tests"
  - "Configure CMake with Tests"

### Visual Studio Integration
1. **Test Explorer**: View → Test Explorer
2. **Run Tests**: Right-click in Test Explorer → "Run"
3. **Debug Tests**: Right-click → "Debug"
4. **Test Output**: View detailed results in Test Explorer

## Test Structure and Patterns

### Test Organization
```
tests/unit_tests/
├── foundation/
│   ├── threading/          # Thread-safety tests
│   ├── containers/         # Container tests  
│   ├── utility/           # Utility tests
│   ├── reactors/          # Reactor pattern tests
│   ├── math/              # Math utility tests
│   └── core/              # Core API tests
├── rendering_context/      # Rendering system tests
└── resource_context/       # Resource management tests
```

### Test Naming Conventions
- **Test Classes**: `ModuleNameTest` (e.g., `Atomic128Test`)
- **Test Methods**: `FeatureName` (e.g., `ConcurrentIncrementStressTest`)
- **Stress Tests**: Include "StressTest" suffix
- **Thread Safety**: Include "ConcurrentAccess" or "ThreadSafety"

### Common Test Patterns

#### Basic Functionality
```cpp
TEST_F(ModuleTest, BasicConstruction) {
    Module module;
    EXPECT_TRUE(module.isValid());
}
```

#### Concurrency Stress Testing
```cpp
TEST_F(ModuleTest, ConcurrentStressTest) {
    constexpr int num_threads = 8;
    constexpr int operations_per_thread = 1000;
    
    std::vector<std::thread> threads;
    // Launch concurrent operations...
    
    // Verify correctness and performance
}
```

#### Memory Ordering Validation
```cpp
TEST_F(ModuleTest, MemoryOrderingTest) {
    // Test happens-before relationships
    // Verify memory visibility across threads
}
```

## Performance and Debugging

### Performance Benchmarks
Most test suites include performance benchmarks that:
- Measure operation latency
- Compare single-threaded vs multi-threaded performance
- Analyze contention overhead
- Validate scalability characteristics

### Debugging Failed Tests
1. **VS Code**: Set breakpoints and use "Debug Test" 
2. **Visual Studio**: Use Test Explorer debug functionality
3. **Command Line**: Run specific tests with `--gtest_filter`
4. **Verbose Output**: Use `--gtest_output=xml` for detailed reporting

### Test Configuration
- **Debug Builds**: Full debugging symbols and assertions
- **Release Builds**: Performance testing and optimization validation
- **Thread Count**: Configurable via test parameters
- **Iteration Count**: Adjustable for stress testing intensity

## Contributing Test Cases

When adding new modules or features:

1. **Create Test Directory**: Follow the established structure
2. **Implement Core Tests**: Basic functionality and edge cases
3. **Add Concurrency Tests**: If the module involves threading
4. **Update CMakeLists.txt**: Add new test targets
5. **Document Test Coverage**: Update this README

### Test Quality Guidelines
- **Deterministic**: Tests should produce consistent results
- **Independent**: Tests should not depend on execution order
- **Fast**: Unit tests should complete quickly
- **Comprehensive**: Cover both happy path and error conditions
- **Thread-Safe**: Concurrent tests should be properly synchronized

## Continuous Integration

The test framework supports CI/CD integration with:
- **XML Output**: JUnit-compatible test reports
- **Return Codes**: Proper exit codes for build systems
- **Parallel Execution**: Faster CI builds
- **Test Filtering**: Run specific test suites in CI stages

Example CI command:
```bash
ctest --test-dir build --output-junit test_results.xml --parallel 4
```

This comprehensive testing framework ensures that the DiamondDogs engine maintains high quality and reliability, particularly for its complex lockless and concurrent systems.
