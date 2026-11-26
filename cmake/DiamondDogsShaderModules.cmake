# One shader bundle per application. A module registers its Slang modules, cook policies, include folders and the
# targets that generate its sources; an application cooks them all with its own shaders in one call, and opens one
# ShaderLibrary. Lodestone merges the policy files in order, and a key that two files set fails the cook.
#
#   diamond_dogs_register_shader_modules(
#       MODULES <file.slang>...
#       [POLICY_FILES <file.toml>...]
#       [INCLUDE_DIRS <dir>...]
#       [DEPENDS <target>...])            # targets that must run before a cook reads the modules
#
#   diamond_dogs_add_shader_cook(<target>
#       OUTPUT_DIR <dir>
#       [OUTPUT_NAME <stem>]              # the stem of every artifact (<stem>.ldmanifest, <stem>.host.hpp); ShaderLibrary without it
#       [HOST_TYPES <header>]             # writes <OUTPUT_DIR>/<stem>.host.hpp: each entry point's Params type
#       MODULES <file.slang>...           # the application's own modules
#       [POLICY_FILES <file.toml>...]
#       [INCLUDE_DIRS <dir>...]
#       [DEPENDS <target>...]
#       [OPTIONS <option>...])
#
# Register before the application's CMake file runs: modules are added before applications.

function(diamond_dogs_register_shader_modules)
    cmake_parse_arguments(PARSE_ARGV 0 SHADERS "" "" "MODULES;POLICY_FILES;INCLUDE_DIRS;DEPENDS")

    if (NOT SHADERS_MODULES)
        message(FATAL_ERROR "diamond_dogs_register_shader_modules: MODULES is empty")
    endif()

    set_property(GLOBAL APPEND PROPERTY DIAMOND_DOGS_SHADER_MODULES ${SHADERS_MODULES})
    set_property(GLOBAL APPEND PROPERTY DIAMOND_DOGS_SHADER_POLICY_FILES ${SHADERS_POLICY_FILES})
    set_property(GLOBAL APPEND PROPERTY DIAMOND_DOGS_SHADER_INCLUDE_DIRS ${SHADERS_INCLUDE_DIRS})
    set_property(GLOBAL APPEND PROPERTY DIAMOND_DOGS_SHADER_DEPENDS ${SHADERS_DEPENDS})
endfunction()

function(diamond_dogs_add_shader_cook target)
    cmake_parse_arguments(PARSE_ARGV 1 COOK "" "OUTPUT_DIR;OUTPUT_NAME;HOST_TYPES" "MODULES;POLICY_FILES;INCLUDE_DIRS;DEPENDS;OPTIONS")

    get_property(registeredModules GLOBAL PROPERTY DIAMOND_DOGS_SHADER_MODULES)
    get_property(registeredPolicies GLOBAL PROPERTY DIAMOND_DOGS_SHADER_POLICY_FILES)
    get_property(registeredIncludes GLOBAL PROPERTY DIAMOND_DOGS_SHADER_INCLUDE_DIRS)
    get_property(registeredDepends GLOBAL PROPERTY DIAMOND_DOGS_SHADER_DEPENDS)

    set(modules ${COOK_MODULES} ${registeredModules})
    set(policies ${COOK_POLICY_FILES} ${registeredPolicies})
    set(includes ${COOK_INCLUDE_DIRS} ${registeredIncludes})
    set(depends ${COOK_DEPENDS} ${registeredDepends})

    foreach (list_name IN ITEMS modules policies includes depends)
        if (${list_name})
            list(REMOVE_DUPLICATES ${list_name})
        endif()
    endforeach()

    set(hostTypes "")

    if (COOK_HOST_TYPES)
        set(hostTypes HOST_TYPES "${COOK_HOST_TYPES}")
    endif()

    set(outputName "")

    if (COOK_OUTPUT_NAME)
        set(outputName OUTPUT_NAME "${COOK_OUTPUT_NAME}")
    endif()

    lodestone_add_shader_cook(${target}
        OUTPUT_DIR "${COOK_OUTPUT_DIR}"
        ${outputName}
        ${hostTypes}
        TARGETS spirv
        POLICY_FILE ${policies}
        INCLUDE_DIRS ${includes}
        OPTIONS ${COOK_OPTIONS}
        MODULES ${modules})

    if (depends)
        add_dependencies(${target} ${depends})
    endif()
endfunction()
