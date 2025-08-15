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
cmake --build build --config Debug --target foundation_tests rendering_context_tests resource_context_tests --parallel

# Build everything
cmake --build build --config Debug --parallel

# Run tests (use VSCode tasks or direct execution)
./build/tests/unit_tests/foundation/Debug/foundation_tests.exe
```

### VS Code Tasks (Preferred Method)
The repository includes pre-configured VS Code tasks:
- **"Build All Tests"** - Builds foundation, rendering_context, and resource_context tests
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
- Rendering context tests: Vulkan device management, swapchain handling  
- Resource context tests: Async GPU resource allocation, memory management

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
│   ├── ContentCompiler/  # Content compilation and processing, coroutine powered data transformation system (in development)
│   ├── RenderingContext/ # Vulkan device/instance management, window system
│   ├── ResourceContext/  # Async GPU resource management with ECS (EnTT)
│   ├── ImGuiModule/     # Debug UI integration
│   ├── RenderGraph/     # Frame graph system (in development)
│   └── MaterialModule/, TerrainModule/, VtfModule/ # Specialized rendering
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

**Key Foundation Components:**
- `threading/`: Lock-free atomics, MWSR queues, critical sections
- `containers/`: Circular buffers, concurrent vectors
- `utility/`: Delegates, tagged types, hash functions
- `reactors/`: Message-passing system components

### Coding Conventions
- **Naming**: PascalCase for public APIs, camelCase for private members, snake_case for parameters
- **Indentation**: 4 spaces (no tabs), braces on new lines
- **Memory**: Always mark functions `constexpr` and `noexcept` when possible
- **Math**: Row-major matrices, right-handed coordinate system, depth range [-1,1]
- **Threading**: Prefer lock-free algorithms over mutex-based synchronization

### Shader Development (Slang)
- Use `.slang` file extensions (NOT .hlsl or .glsl)
- Specialization constants with `[SpecializationConstant]` attribute
- Import syntax: `import moduleName;` for modules, `__include fileName;` for includes
- Resource bindings use descriptor indexing and bindless patterns

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
- Rendering context changes require integration test validation
- Resource context modifications need async operation testing
- Slang shader changes require manual compilation verification

## Agent Guidelines

**Trust These Instructions**: This document has been validated through actual repository exploration and build testing. Only search for additional information if these instructions are incomplete or incorrect.

**Common Tasks & Approaches:**
- **Adding new foundation utilities**: Follow existing patterns in `foundation/include/` and `foundation/src/`
- **Creating new modules**: Use `Modules/` directory structure with CMakeLists.txt and proper dependencies
- **Shader work**: Use Slang syntax, not HLSL/GLSL; leverage specialization constants
- **Threading code**: Prefer lock-free patterns; use foundation's MWSR queues for module communication
- **Graphics features**: Build on RenderingContext and ResourceContext; use VulpesRender wrapper APIs
- **Testing**: Add unit tests to appropriate subdirectories; use Google Test framework

**Performance Considerations:**
- Full builds take 2-5 minutes depending on hardware
- CMake configuration takes ~20-30 seconds due to dependency resolution
- Unit tests execute quickly (<1 minute total)
- Integration tests may take longer due to Vulkan initialization

**Debugging Support:**
- VS Code has pre-configured debugging for unit tests
- Vulkan validation layers enabled by default in Debug builds
- Extensive debug object naming and utilities in RenderingContext
- Foundation includes debug assertion systems and logging

This repository represents years of iterative development focused on learning and experimentation rather than shipping products. Treat it as a research codebase where modern C++ and Vulkan techniques take precedence over compatibility or production concerns.
