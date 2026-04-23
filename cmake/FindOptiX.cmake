# SPDX-FileCopyrightText: Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

if(TARGET OptiX::OptiX)
    return()
endif()

find_package(CUDAToolkit REQUIRED)

set(OptiX_INSTALL_DIR "OptiX_INSTALL_DIR-NOTFOUND" CACHE PATH "Path to the installed location of the OptiX SDK.")

function(_optix_extract_version optix_sdk_dir out_var)
    set(_optix_header "${optix_sdk_dir}/include/optix.h")
    set(_optix_version "")

    if(EXISTS "${_optix_header}")
        file(READ "${_optix_header}" _optix_header_contents)
        string(REGEX MATCH "OPTIX_VERSION ([0-9]+)([0-9][0-9])([0-9][0-9])" _optix_version_match "${_optix_header_contents}")
        if(_optix_version_match)
            set(_optix_version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
        endif()
    endif()

    set(${out_var} "${_optix_version}" PARENT_SCOPE)
endfunction()

function(_optix_select_sdk_dir out_var)
    set(_optix_best_dir "")
    set(_optix_best_version "")

    foreach(_optix_dir IN LISTS ARGN)
        _optix_extract_version("${_optix_dir}" _optix_dir_version)
        if(NOT _optix_dir_version)
            continue()
        endif()

        if(OptiX_FIND_VERSION)
            if(OptiX_FIND_VERSION_EXACT)
                if(NOT _optix_dir_version VERSION_EQUAL OptiX_FIND_VERSION)
                    continue()
                endif()
            elseif(_optix_dir_version VERSION_LESS OptiX_FIND_VERSION)
                continue()
            endif()
        endif()

        if(NOT _optix_best_dir OR _optix_dir_version VERSION_GREATER _optix_best_version)
            set(_optix_best_dir "${_optix_dir}")
            set(_optix_best_version "${_optix_dir_version}")
        endif()
    endforeach()

    set(${out_var} "${_optix_best_dir}" PARENT_SCOPE)
endfunction()

set(OptiX_SDK_VERSION_GLOB "*")
if(OptiX_FIND_VERSION_EXACT AND OptiX_FIND_VERSION)
    set(OptiX_SDK_VERSION_GLOB "${OptiX_FIND_VERSION}")
endif()

# If they haven't specified a specific OptiX SDK install directory, search likely default locations for SDKs.
if(NOT OptiX_INSTALL_DIR)
    set(_optix_all_sdk_dirs "")
    if(CMAKE_HOST_WIN32)
        # This is the default OptiX SDK install location on Windows.
        file(GLOB _optix_all_sdk_dirs "$ENV{ProgramData}/NVIDIA Corporation/OptiX SDK ${OptiX_SDK_VERSION_GLOB}*")
    else()
        # On linux, there is no default install location for the SDK, but it does have a default subdir name.
        # Scan all candidate directories to find the newest SDK that satisfies the version requirement.
        foreach(dir "/opt" "/usr/local" "$ENV{HOME}" "$ENV{HOME}/Downloads")
            file(GLOB _optix_found_dirs "${dir}/NVIDIA-OptiX-SDK-${OptiX_SDK_VERSION_GLOB}*")
            list(APPEND _optix_all_sdk_dirs ${_optix_found_dirs})
        endforeach()
    endif()

    # Pick the newest SDK by comparing the OptiX version in each candidate's
    # include/optix.h, not by sorting the full installation path.
    list(LENGTH _optix_all_sdk_dirs _optix_len)
    if(_optix_len GREATER 0)
        list(REMOVE_DUPLICATES _optix_all_sdk_dirs)
        _optix_select_sdk_dir(OPTIX_SDK_DIR ${_optix_all_sdk_dirs})
    endif()
endif()

find_path(OptiX_ROOT_DIR NAMES include/optix.h PATHS ${OptiX_INSTALL_DIR} ${OPTIX_SDK_DIR} /opt/optix /opt/optix-dev /usr/local/optix /usr/local/optix-dev REQUIRED)
file(READ "${OptiX_ROOT_DIR}/include/optix.h" header)
string(REGEX REPLACE "^.*OPTIX_VERSION ([0-9]+)([0-9][0-9])([0-9][0-9])[^0-9].*$" "\\1.\\2.\\3" OPTIX_VERSION ${header})
string(REGEX REPLACE "^.*OPTIX_VERSION ([0-9]+)[^0-9].*$" "\\1" OptiX_VERSION ${header})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OptiX
    FOUND_VAR OptiX_FOUND
    VERSION_VAR OPTIX_VERSION
    REQUIRED_VARS
        OptiX_ROOT_DIR
    REASON_FAILURE_MESSAGE
        "OptiX installation not found. Please use CMAKE_PREFIX_PATH or OptiX_INSTALL_DIR to locate 'include/optix.h'."
)

set(OptiX_INCLUDE_DIR ${OptiX_ROOT_DIR}/include)

add_library(OptiX::OptiX INTERFACE IMPORTED)
target_include_directories(OptiX::OptiX INTERFACE ${OptiX_INCLUDE_DIR} ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(OptiX::OptiX INTERFACE ${CMAKE_DL_LIBS})
