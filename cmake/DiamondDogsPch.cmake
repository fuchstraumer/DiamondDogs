# Precompiled headers for the targets that DiamondDogs owns. Never use this on a third-party target.
#
# A target calls diamond_dogs_add_pch(<target> <LIBRARY|TEST>). ADD_MODULE, add_unit_test_target and
# ADD_INTEGRATION_TEST already do this. The call only records the target. When the root CMakeLists.txt ends, every link
# and every compile setting of every target is final, and diamond_dogs_resolve_pch() sorts the recorded targets by their
# compile settings: the first target of a group makes the PCH, and each other target of the group reuses it
# (REUSE_FROM). A target whose compile settings match no earlier target gets a PCH of its own.
#
# Configure with -DDIAMOND_DOGS_USE_PCH=OFF to build without any PCH. A PCH hides missing includes, so that build is how
# we check that each file includes what it uses.

option(DIAMOND_DOGS_USE_PCH "Use precompiled headers for the targets that DiamondDogs owns" ON)

set(DIAMOND_DOGS_PCH_LIBRARY_HEADER "${CMAKE_CURRENT_LIST_DIR}/pch/DiamondDogsPch.hpp")
set(DIAMOND_DOGS_PCH_TEST_HEADER "${CMAKE_CURRENT_LIST_DIR}/pch/DiamondDogsTestPch.hpp")

# The properties that decide whether two targets can share a PCH: the compile definitions and the compile options, from
# the target, from the directory that created it, and from every library that passes them on through its interface
set(DIAMOND_DOGS_PCH_OWN_PROPERTIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
set(DIAMOND_DOGS_PCH_INTERFACE_PROPERTIES INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_OPTIONS)
set(DIAMOND_DOGS_PCH_LANGUAGE_PROPERTIES CXX_STANDARD CXX_EXTENSIONS MSVC_RUNTIME_LIBRARY INTERPROCEDURAL_OPTIMIZATION)

function(diamond_dogs_add_pch TARGET KIND)
    if(NOT DIAMOND_DOGS_USE_PCH)
        return()
    endif()

    if(NOT KIND STREQUAL "LIBRARY" AND NOT KIND STREQUAL "TEST")
        message(FATAL_ERROR "diamond_dogs_add_pch: the kind of ${TARGET} must be LIBRARY or TEST, not ${KIND}")
    endif()

    get_property(already_deferred GLOBAL PROPERTY DIAMOND_DOGS_PCH_RESOLVE_DEFERRED)
    if(NOT already_deferred)
        set_property(GLOBAL PROPERTY DIAMOND_DOGS_PCH_RESOLVE_DEFERRED TRUE)
        cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL diamond_dogs_resolve_pch)
    endif()

    set_property(GLOBAL APPEND PROPERTY DIAMOND_DOGS_PCH_TARGETS "${KIND}|${TARGET}")
endfunction()

# Appends one "<label>:<item>" setting for each item of ARGN to the variable that VARIABLE names
function(diamond_dogs_pch_append_settings VARIABLE LABEL)
    set(updated "${${VARIABLE}}")

    foreach(item IN LISTS ARGN)
        list(APPEND updated "${LABEL}:${item}")
    endforeach()

    set(${VARIABLE} "${updated}" PARENT_SCOPE)
endfunction()

# Sets the variable named by OUT_SIGNATURE to the sorted, unique settings that the compiler sees for TARGET
function(diamond_dogs_pch_signature TARGET OUT_SIGNATURE)
    set(settings "")

    foreach(property IN LISTS DIAMOND_DOGS_PCH_OWN_PROPERTIES DIAMOND_DOGS_PCH_LANGUAGE_PROPERTIES)
        get_target_property(value ${TARGET} ${property})
        if(value)
            diamond_dogs_pch_append_settings(settings ${property} ${value})
        endif()
    endforeach()

    get_target_property(source_directory ${TARGET} SOURCE_DIR)
    foreach(property IN LISTS DIAMOND_DOGS_PCH_OWN_PROPERTIES)
        get_property(value DIRECTORY "${source_directory}" PROPERTY ${property})
        if(value)
            diamond_dogs_pch_append_settings(settings ${property} ${value})
        endif()
    endforeach()

    # a library passes its interface on to the targets that link it, and its own public libraries on with it
    get_target_property(pending ${TARGET} LINK_LIBRARIES)
    set(visited "")
    while(pending)
        list(POP_FRONT pending library)
        if(NOT TARGET "${library}" OR library IN_LIST visited)
            continue()
        endif()

        list(APPEND visited "${library}")

        foreach(property IN LISTS DIAMOND_DOGS_PCH_INTERFACE_PROPERTIES)
            get_target_property(value ${library} ${property})
            if(value)
                string(REPLACE "INTERFACE_" "" own_property "${property}")
                diamond_dogs_pch_append_settings(settings ${own_property} ${value})
            endif()
        endforeach()

        get_target_property(passed_on ${library} INTERFACE_LINK_LIBRARIES)
        if(passed_on)
            list(APPEND pending ${passed_on})
        endif()
    endwhile()

    list(REMOVE_DUPLICATES settings)
    list(SORT settings)
    set(${OUT_SIGNATURE} "${settings}" PARENT_SCOPE)
endfunction()

function(diamond_dogs_resolve_pch)
    get_property(entries GLOBAL PROPERTY DIAMOND_DOGS_PCH_TARGETS)

    foreach(entry IN LISTS entries)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 kind)
        list(GET parts 1 target)

        diamond_dogs_pch_signature(${target} signature)
        string(MD5 group "${kind}|${signature}")
        get_property(owner GLOBAL PROPERTY DIAMOND_DOGS_PCH_OWNER_${group})

        if(owner)
            target_precompile_headers(${target} REUSE_FROM ${owner})
            message(VERBOSE "PCH: ${target} reuses the PCH of ${owner}")
            continue()
        endif()

        if(kind STREQUAL "TEST")
            set(header "${DIAMOND_DOGS_PCH_TEST_HEADER}")
        else()
            set(header "${DIAMOND_DOGS_PCH_LIBRARY_HEADER}")
        endif()

        # the PCH includes vulkan_core.h, and a target that links only foundation does not see the Vulkan folder
        target_include_directories(${target} PRIVATE ${Vulkan_INCLUDE_DIR})
        target_precompile_headers(${target} PRIVATE "${header}")
        set_property(GLOBAL PROPERTY DIAMOND_DOGS_PCH_OWNER_${group} ${target})
        message(VERBOSE "PCH: ${target} makes a PCH (${kind})")
    endforeach()
endfunction()
