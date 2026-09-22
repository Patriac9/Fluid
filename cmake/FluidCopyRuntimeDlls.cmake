# POST_BUILD helper: copy runtime DLLs next to a Windows executable.
# Included from CMakeLists to define fluid_copy_runtime_dlls().
# Invoked again with cmake -P after each build to resolve and copy files.

if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT TARGET_FILE OR NOT DEST_DIR)
        message(FATAL_ERROR "FluidCopyRuntimeDlls: TARGET_FILE and DEST_DIR are required")
    endif()
    if(NOT EXISTS "${TARGET_FILE}")
        message(FATAL_ERROR "FluidCopyRuntimeDlls: missing ${TARGET_FILE}")
    endif()

    set(_search_dirs)
    if(SEARCH_DIRS)
        # "@@" rather than "|" — cmd.exe treats an unquoted pipe as a command separator.
        string(REPLACE "@@" ";" _search_dirs "${SEARCH_DIRS}")
    endif()

    set(_path_prefix "")
    foreach(_dir IN LISTS _search_dirs)
        if(EXISTS "${_dir}")
            file(TO_NATIVE_PATH "${_dir}" _native)
            string(APPEND _path_prefix "${_native};")
        endif()
    endforeach()
    if(_path_prefix)
        set(ENV{PATH} "${_path_prefix}$ENV{PATH}")
    endif()

    function(_fluid_copy_dll _dll)
        if(EXISTS "${_dll}")
            execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "${DEST_DIR}"
                RESULT_VARIABLE _result)
            if(_result)
                message(WARNING "FluidCopyRuntimeDlls: failed to copy ${_dll}")
            endif()
        endif()
    endfunction()

    if(GLFW_DLL)
        _fluid_copy_dll("${GLFW_DLL}")
    endif()
    if(VULKAN_DLL)
        _fluid_copy_dll("${VULKAN_DLL}")
    endif()

    set(_mingw_runtime
        libgcc_s_seh-1.dll
        libgcc_s_dw2-1.dll
        libstdc++-6.dll
        libwinpthread-1.dll
        libssp-0.dll
        libatomic-1.dll)
    foreach(_name IN LISTS _mingw_runtime)
        foreach(_dir IN LISTS _search_dirs)
            _fluid_copy_dll("${_dir}/${_name}")
        endforeach()
    endforeach()

    if(TARGET_KIND STREQUAL "library")
        file(GET_RUNTIME_DEPENDENCIES
            LIBRARIES "${TARGET_FILE}"
            RESOLVED_DEPENDENCIES_VAR _resolved
            UNRESOLVED_DEPENDENCIES_VAR _unresolved
            DIRECTORIES ${_search_dirs}
            PRE_EXCLUDE_REGEXES
                [[api-ms-win-.*]]
                [[ext-ms-.*]]
                [[hvsifiletrust\.dll]]
                [[pdmutilities\.dll]]
            POST_EXCLUDE_REGEXES
                [[.*[/\\][Ww]indows[/\\][Ss]ystem32[/\\].*]]
                [[.*[/\\][Ww]indows[/\\][Ss]ysWOW64[/\\].*]])
    else()
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES "${TARGET_FILE}"
            RESOLVED_DEPENDENCIES_VAR _resolved
            UNRESOLVED_DEPENDENCIES_VAR _unresolved
            DIRECTORIES ${_search_dirs}
            PRE_EXCLUDE_REGEXES
                [[api-ms-win-.*]]
                [[ext-ms-.*]]
                [[hvsifiletrust\.dll]]
                [[pdmutilities\.dll]]
            POST_EXCLUDE_REGEXES
                [[.*[/\\][Ww]indows[/\\][Ss]ystem32[/\\].*]]
                [[.*[/\\][Ww]indows[/\\][Ss]ysWOW64[/\\].*]])
    endif()
    foreach(_dll IN LISTS _resolved)
        file(TO_CMAKE_PATH "${_dll}" _normalized)
        string(TOLOWER "${_normalized}" _lower)
        if(_lower MATCHES "/windows/" AND
           (_lower MATCHES "/system32/" OR _lower MATCHES "/syswow64/" OR _lower MATCHES "/winsxs/"))
            continue()
        endif()
        _fluid_copy_dll("${_dll}")
    endforeach()
    return()
endif()

function(fluid_copy_runtime_dlls target)
    if(NOT WIN32)
        return()
    endif()
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "fluid_copy_runtime_dlls: unknown target '${target}'")
    endif()

    get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(_search_dirs "${_compiler_bin}")
    if(DEFINED ENV{VULKAN_SDK} AND NOT "$ENV{VULKAN_SDK}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" _vk_sdk)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            list(APPEND _search_dirs "${_vk_sdk}/Bin")
        else()
            list(APPEND _search_dirs "${_vk_sdk}/Bin32")
        endif()
    endif()
    if(Vulkan_LIBRARY)
        get_filename_component(_vk_libdir "${Vulkan_LIBRARY}" DIRECTORY)
        get_filename_component(_vk_root "${_vk_libdir}" DIRECTORY)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            list(APPEND _search_dirs "${_vk_root}/Bin")
        else()
            list(APPEND _search_dirs "${_vk_root}/Bin32")
        endif()
    endif()
    list(REMOVE_DUPLICATES _search_dirs)
    string(REPLACE ";" "@@" _search_arg "${_search_dirs}")

    set(_vulkan_dll "")
    if(NOT FLUID_VULKAN_DLL)
        find_file(FLUID_VULKAN_DLL NAMES vulkan-1.dll HINTS ${_search_dirs} PATHS ENV PATH)
    endif()
    if(FLUID_VULKAN_DLL)
        set(_vulkan_dll "${FLUID_VULKAN_DLL}")
    endif()

    set(_glfw_dll "")
    if(TARGET glfw)
        get_target_property(_glfw_type glfw TYPE)
        if(_glfw_type STREQUAL "SHARED_LIBRARY")
            set(_glfw_dll "$<TARGET_FILE:glfw>")
        endif()
    endif()

    set(_target_kind executable)
    get_target_property(_target_type ${target} TYPE)
    if(_target_type STREQUAL "SHARED_LIBRARY" OR _target_type STREQUAL "MODULE_LIBRARY")
        set(_target_kind library)
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DTARGET_FILE=$<TARGET_FILE:${target}>"
            "-DDEST_DIR=$<TARGET_FILE_DIR:${target}>"
            "-DSEARCH_DIRS=${_search_arg}"
            "-DVULKAN_DLL=${_vulkan_dll}"
            "-DGLFW_DLL=${_glfw_dll}"
            "-DTARGET_KIND=${_target_kind}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/FluidCopyRuntimeDlls.cmake"
        COMMENT "Copy runtime DLLs next to ${target}"
        VERBATIM)
endfunction()
