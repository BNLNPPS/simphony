# SPDX-FileCopyrightText: Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
#

if(TARGET OptiX::OptiX)
    return()
endif()

find_package(CUDAToolkit REQUIRED)

set(OptiX_INSTALL_DIR "OptiX_INSTALL_DIR-NOTFOUND" CACHE PATH "Path to the installed location of the OptiX SDK.")

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

    # Pick the SDK with the highest version number across all candidate directories.
    list(LENGTH _optix_all_sdk_dirs _optix_len)
    if(_optix_len GREATER 0)
        list(SORT _optix_all_sdk_dirs COMPARE NATURAL)
        list(REVERSE _optix_all_sdk_dirs)
        list(GET _optix_all_sdk_dirs 0 OPTIX_SDK_DIR)
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
