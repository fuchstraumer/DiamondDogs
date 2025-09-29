# DiamondDogs - Copilot Coding Agent Instructions

## Repository Overview

**DiamondDogs** is a modern C++ graphics engine project focused on experimenting with cutting-edge rendering techniques. This is NOT a production game engine - it's a research playground and technology demonstrator for advanced Vulkan-based graphics programming.

### High-Level Technical Details
- **Primary Language**: C++23 with latest features (coroutines, concepts, modules)
- **Graphics API**: Vulkan 1.4+ with dynamic rendering and modern extensions
- **Shader Language**: Slang (NOT HLSL/GLSL) - use `.slang` file extensions
- **Build System**: CMake with Visual Studio 2022 generator
- **Memory Management**: Mimalloc override + Vulkan Memory Allocator (VMA)
- **Threading**: Lock-free data structures, message-passing design patterns
- **Architecture**: Static library modules, NOT dynamic plugins
- **Repository Size**: Medium (~50MB including submodules)
- **Target Platform**: Windows only (requires Vulkan 1.4+, Visual Studio 2022)

### Core Dependencies
- **Vulkan SDK**: 1.4.321+ required
- **Visual Studio**: 2022 Community or higher 
- **CMake**: 4.1+ (tested version)
- **PowerShell**: 7.5+ (for build scripts)
- **Git**: Required for submodule management

## Build Instructions

### Initial Setup (CRITICAL - ALWAYS RUN FIRST)
```powershell
# 1. Configure CMake - ALWAYS run this first for new clones
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# 2. Build foundation tests to verify setup
cmake --build build --config Debug --target foundation_tests --parallel
```

### Standard Build Process
```powershell
# Build all unit tests (recommended for validation)
cmake --build build --config Debug --target foundation_tests rhi_system_tests platform_system_tests resource_context_tests --parallel

# Build everything
cmake --build build --config Debug --parallel

# Run tests (use VSCode tasks or direct execution)
./build/tests/unit_tests/foundation/Debug/foundation_tests.exe
```

### VS Code Tasks (Preferred Method)
The repository includes pre-configured VS Code tasks:
- **"Build All Tests"** - Builds foundation, rhi_system, platform_system, and resource_context tests
- **"Configure CMake with Tests"** - Initial CMake configuration 
- **"Run All Tests"** - Uses CTest to run all unit tests
- **"Clean and Rebuild Tests"** - Full clean rebuild

Use `Ctrl+Shift+P` → "Tasks: Run Task" to access these.

### Build Troubleshooting
**Common Issues & Solutions:**
- **Submodule errors**: Run `git submodule update --init --recursive`
- **Vulkan not found**: Install Vulkan SDK 1.4.321+ and restart terminal
- **CMake generation fails**: Delete `build/` directory and reconfigure
- **Missing dependencies**: Ensure VS2022 C++ workload is installed
- **Long build times**: Use `--parallel` flag, expect 2-5 minutes for full builds

### Testing
**Unit Tests**: Located in `tests/unit_tests/` with Google Test framework
- Foundation tests: Threading primitives, lock-free containers, utilities
- RHI system tests: Vulkan API abstraction, typed handles, device lifecycle
- Platform system tests: Window management, display detection, input handling
- RHI resources tests: Async GPU resource allocation, memory management (renamed ResourceContext)

**Integration Tests**: Located in `tests/integration_tests/` 
- TriangleTest: Basic Vulkan rendering pipeline
- ResourceContextTest: Complex async resource operations

**Do NOT run CTest directly** - use VS Code tasks or individual test executables.

## Project Architecture & Layout

### Directory Structure
```
DiamondDogs/
├── foundation/           # Core utilities, threading, containers (like engine stdlib)
├── Modules/             # Discrete, composable functionality modules
│   ├── RhiSystem/       # Modern Vulkan abstraction with typed handles (RhiHandle<T>)
│   ├── PlatformSystem/  # Cross-platform window/display/swapchain management
│   ├── RhiResources/    # GPU resource allocation and management (renamed ResourceContext)
│   ├── RenderGraph/     # Frame graph system for rendering (in development)
│   ├── ImGuiModule/     # Debug UI integration (inactive)
│   └── ContentCompiler/, MaterialModule/, TerrainModule/, VtfModule/ # Specialized systems (inactive)
├── tests/               # Unit and integration tests
│   ├── unit_tests/      # Google Test-based component tests
│   └── integration_tests/ # Full module integration tests
├── third_party/         # All external dependencies as submodules
├── assets/              # Test data, shaders, configuration files
├── cmake/               # CMake utilities and configuration scripts
└── .vscode/             # Pre-configured build tasks and settings
```

### Key Files & Configuration
- **CMakeLists.txt**: Root build configuration with dependency management
- **foundation/CMakeLists.txt**: Core library build rules
- **.vscode/tasks.json**: Pre-configured build and test tasks
- **.vscode/settings.json**: C++ IntelliSense configuration with test discovery
- **assets/logging.ini**: Runtime logging configuration
- **tests/integration_tests/*/cfg/*.json**: Test configuration files

### Module Dependencies & Architecture
The project uses "inverted hierarchy" architecture:
1. **Foundation** → Core functionality (threading, utilities, math)
2. **Modules** → Build on foundation and each other
3. **Applications** → Compose modules into complete programs
4. **Tests** → Validate individual modules and their integration

**Architectural Evolution**: The RhiSystem and PlatformSystem represent a split from the previous monolithic RenderingContext module. RhiSystem focuses purely on Vulkan/graphics API abstraction using typed handles (`RhiHandle<T>`) and can operate without presentation (useful for compute-only workloads). PlatformSystem handles windowing, swapchain management, and input. This separation removes the VulpesRender dependency in favor of direct Vulkan object management with potential future DirectX 12 compatibility.

**Key Foundation Components:**
- `threading/`: Lock-free atomics, MWSR queues, critical sections, SRW locks
- `containers/`: Circular buffers, concurrent vectors, concurrent deque
- `utility/`: Delegates (prefer over std::function), tagged types, hash functions
- `events/`: Platform events, RHI events, display events
- Plugin system with PDB repair utilities (Win32)

### Coding Conventions

#### Code Formatting Rules
- **Single-line if statements**: NEVER allowed. All if statements must include brackets placed on a newline
- **Function implementations**: Can NEVER be placed in headers, even for single-line getters. All implementation goes in source files
- **Indentation**: 4 spaces always (no tabs) for cross-platform consistency
- **Brackets**: Always go on new lines
- **Control Flow**: Always use braces for if statements, even single-line ones
- **Naming**: PascalCase for public APIs, camelCase for private members, snake_case for parameters
- **Member prefixes**: `m_` must NEVER be used as prefix for member variables
- **Constructor initializers**: Colon on same line as declaration, each initializer on new line with trailing comma:
```cpp
Struct::Struct(int _val0, int _val1, int _val2) :
  val0{ _val0 },
  val1{ _val1 },
  val2{ _val2 }
{}
```

#### C++ Language Preferences
- **Functions**: No implementations in headers; mark `constexpr` and `noexcept` when possible
- **Constructors**: Should be `noexcept` when possible
- **Move/copy operators**: Define `noexcept` versions when beneficial
- **Auto usage**: Minimize except for iterators/complex nested types (e.g., `auto iter = map.find(key)` OK, `auto value = vector.front()` not OK)
- **Virtual classes**: Use `final` when possible to collapse vtables and improve performance
- **Error handling**: Use `Result` types for function return status within RHI code; avoid exceptions

#### Enum Formatting
- Use smallest bitwidth type possible, always prefer `enum class` for scoping
- For distinct values (non-bitmask): First value should be `None` with value `0`
- For bitmask enums: First value should be `Invalid` with value `0`
- For bitmask enums: Add operators for at least `|` and `&` operations
- Boolean conversion operators are preferable

#### Memory & Performance
- **Threading**: Prefer lock-free algorithms over mutex-based synchronization
- **Memory**: Minimize `auto` usage except for iterators/complex nested types
- **Span**: Use `std::span` for array parameters instead of raw pointers + size
- **Math**: Row-major matrices, right-handed coordinate system, depth range [-1,1]
- **String conversion**: Use `charconv` instead of C conversion functions for string/char to integral types
- **Error Handling**: Use `Result` types for function return status within RHI code; avoid exceptions

### C++ Standard Library Usage
- Use `std::upper_bound` and `std::lower_bound` from `<algorithm>` when possible
- Retrieve numerical constants from `<numbers>` header
- Minimize standard library includes across module boundaries
- Use foundation's delegate type instead of `std::function`

### CMake Best Practices
- Use `add_custom_command` with proper OUTPUT and DEPENDS instead of `add_custom_target`
- Track explicit dependencies on external files (Vulkan XML specs) for proper rebuilds
- Ensure slow generators only run when inputs change
- Use VERBATIM for better cross-platform command handling

### Math Conventions for Vector/Matrix Operations
- Assume all vectors are column vectors; matrices in column-major format
- Slang uses row-major storage: `transformed_vec = mul(vec, mat)` (vec as row vector)
- Use "RH" (right-handed) versions of matrix functions (avoids DirectX conventions)
- Remember depth range is [-1,1], not [0,1]

### Shader Development (Slang)

#### Basic Conventions
- Use `.slang` file extensions (NOT .hlsl or .glsl)
- Specialization constants with `[SpecializationConstant]` attribute for compile-time branching
- Import syntax: `import moduleName;` for modules, `__include fileName;` for includes
- Resource bindings use descriptor indexing and bindless patterns
- Use `uniform` qualifier for resource pointer parameters (becomes push constants)
- Mark internal functions as `internal` to avoid namespace pollution
- Use `float3` consistently for 3D vectors unless `float4` needed for alignment/math
- Avoid naming .slang files the same as entrypoints (confuses slangc tooling)

#### Performance Optimization
- Prefer wave operations (`WaveActiveSum`, `WavePrefixSum`, `WaveIsFirstLane`) for GPU efficiency
- Use `GroupMemoryBarrierWithGroupSync()` for full synchronization, `GroupMemoryBarrier()` only for memory visibility
- Minimize synchronization points - each barrier impacts GPU performance significantly
- Calculate per-thread rather than using shared memory + barriers when arithmetic is cheaper
- Use early-exit strategies in branched code, but prefer branchless approaches
- For reductions: wave-reduce within waves first, then reduce across wave results

#### Resource Management
- Structure resource pointers in dedicated structs: `SomethingResourcePointers`
- Resource pointers only work for linear arrays of values (including composite structs)
- Cannot store resource pointers to textures or image resources
- For bindless textures: Use `DescriptorHandle<T>` with push constant arrays of structs

#### Module and Import Conventions
- Always rebuild `.slang-module` files after changes to avoid missing symbol errors
- Use `import moduleName;` for external modules, `__include fileName;` for same-module includes
- Module content access doesn't need qualification
- View space coordinates preferred for clustering/culling algorithms
- BVH traversal uses view space positions for consistent coordinate system

## Validation & CI

**No GitHub Actions**: Repository has no automated CI/CD pipelines. All validation is local.

**Manual Validation Steps:**
1. Build all test targets successfully
2. Run foundation_tests.exe - validates core threading and containers
3. Run integration tests for modules being modified
4. Verify no new compiler warnings in Debug configuration
5. Test with Vulkan validation layers enabled (default in Debug)

**Critical Validation Notes:**
- Always build and run foundation tests after foundation changes
- RHI system changes require Vulkan validation layers enabled
- Platform system changes need multi-monitor/window testing
- RHI resources modifications need async operation testing
- Slang shader changes require manual compilation verification

## Agent Guidelines

**Trust These Instructions**: This document has been validated through actual repository exploration and build testing. Only search for additional information if these instructions are incomplete or incorrect.

**Common Tasks & Approaches:**
- **Adding new foundation utilities**: Follow existing patterns in `foundation/include/` and `foundation/src/`
- **Creating new modules**: Use `Modules/` directory structure with CMakeLists.txt and proper dependencies
- **Shader work**: Use Slang syntax with specialization constants; rebuild .slang-module files after changes
- **Threading code**: Prefer lock-free patterns; use foundation's MWSR queues for module communication
- **Graphics features**: Build on RhiSystem and RhiResources; use typed handles (`RhiHandle<T>`) for API abstraction
- **Platform code**: Use PlatformSystem for window/display/swapchain management, not direct GLFW
- **Testing**: Add unit tests to appropriate subdirectories; use Google Test framework

**Performance Considerations:**
- Full builds take 2-5 minutes depending on hardware
- CMake configuration takes ~20-30 seconds due to dependency resolution
- Unit tests execute quickly (<1 minute total)
- Integration tests may take longer due to Vulkan initialization

**Debugging Support:**
- VS Code has pre-configured debugging for unit tests
- Vulkan validation layers enabled by default in Debug builds
- Extensive debug object naming and utilities in RhiSystem
- Foundation includes debug assertion systems and logging

This repository represents years of iterative development focused on learning and experimentation rather than shipping products. Treat it as a research codebase where modern C++ and Vulkan techniques take precedence over compatibility or production concerns.
