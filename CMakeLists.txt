cmake_minimum_required(VERSION 3.22)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 75)
endif()

project(simphony VERSION 0.7.0 LANGUAGES CXX CUDA)

# Use CMake's standard testing option, but avoid enabling this project's tests
# by default when Simphony is pulled in as a subproject.
if(NOT DEFINED BUILD_TESTING)
    set(BUILD_TESTING ${PROJECT_IS_TOP_LEVEL} CACHE BOOL "Build the testing tree.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 20 not 17: headers shared between C++ and CUDA (e.g. sysrap/torch.h) use
# C++20 designated initializers, which GNU-mode nvcc tolerates in C++17 as an
# extension but MSVC-host nvcc rejects.
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(MSVC)
    # MSVC's <cmath> defines M_PI and friends only under this macro
    add_compile_definitions(_USE_MATH_DEFINES)
    # windows.h min/max macros break std::min/std::max wherever a header
    # (e.g. optix_stubs.h) includes windows.h ahead of sysrap/s_windows.h
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
set(CMAKE_CUDA_EXTENSIONS OFF)

# Build only the GPU core packages (SysRap, CSG, QUDArap, CSGOptiX), omitting
# the packages that require Geant4 (u4, g4cx, src). Selection must happen at
# configure time, not by choosing a target: u4 and src call
# find_package(Geant4 REQUIRED), so a full configure fails outright where
# Geant4 is absent. Defaults ON where Geant4 is not available in this stack.
if(WIN32)
    set(_simphony_core_only_default ON)
else()
    set(_simphony_core_only_default OFF)
endif()
option(SIMPHONY_CORE_ONLY
    "Build only the GPU core packages, omitting the Geant4-dependent ones"
    ${_simphony_core_only_default})

# The interactive visualisation support in SysRap (SGLFW*, SGLM*, SGLDisplay)
# is header-only: no library source compiles it, so GLEW and GLFW are link
# dependencies of consumers, not of the core libraries themselves. A headless
# worker needs neither. Off with the core-only build, on otherwise.
if(SIMPHONY_CORE_ONLY)
    set(_simphony_with_viz_default OFF)
else()
    set(_simphony_with_viz_default ON)
endif()
option(SIMPHONY_WITH_VIZ
    "Link the OpenGL/GLFW interactive visualisation dependencies"
    ${_simphony_with_viz_default})

# Shared libraries by default. Windows builds static: exporting these packages
# from DLLs would mean carrying templates, STL members and static data across
# the boundary, and the coprocessor worker ships as one self-contained
# executable regardless. Setting BUILD_SHARED_LIBS ON explicitly on Windows is
# still supported: it defines SIMPHONY_SHARED_LIBS, which switches the
# *_API_EXPORT.hh headers to __declspec.
if(NOT DEFINED BUILD_SHARED_LIBS)
    if(WIN32)
        set(BUILD_SHARED_LIBS OFF)
    else()
        set(BUILD_SHARED_LIBS ON)
    endif()
endif()
if(WIN32 AND BUILD_SHARED_LIBS)
    add_compile_definitions(SIMPHONY_SHARED_LIBS)
endif()

if(MSVC)
    # The utility layer uses the POSIX spellings (strdup, fopen, getenv) that
    # the Windows CRT marks deprecated in favour of its own _-prefixed and
    # _s variants. The functions are present and correct; only the warnings
    # are unwanted.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
endif()

include(GNUInstallDirs)
# Keep CTest included even when BUILD_TESTING=OFF so external drivers can run
# ctest and observe an empty test set rather than a missing configuration.
include(CTest)
include(CheckLinkerFlag)

add_library(simphony_argparse INTERFACE)
target_include_directories(simphony_argparse INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/third_party>
)

# Make installed binaries self-describing at runtime:
# - use $ORIGIN so executables/libraries can find Simphony's own shared libs
# - append linked dependency directories automatically
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
    set(CMAKE_INSTALL_RPATH "\$ORIGIN;\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
    # Prefer transitive DT_RPATH over DT_RUNPATH so container runtimes such as
    # Apptainer can resolve indirect dependencies reliably.
    check_linker_flag(CXX "LINKER:--disable-new-dtags" SIMPHONY_HAVE_DISABLE_NEW_DTAGS)
    if(SIMPHONY_HAVE_DISABLE_NEW_DTAGS)
        add_link_options("LINKER:--disable-new-dtags")
    endif()
endif()

# used by sysrap/config_path.h.in
set(GPHOX_INSTALL_FULL_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}")
set(GPHOX_BUILD_PTX_DIR "${PROJECT_BINARY_DIR}/ptx")

add_subdirectory(sysrap)
add_subdirectory(CSG)
add_subdirectory(qudarap)
add_subdirectory(CSGOptiX)
if(NOT SIMPHONY_CORE_ONLY)
    add_subdirectory(u4)
    add_subdirectory(g4cx)
    add_subdirectory(src)
endif()

if(BUILD_TESTING)
    add_subdirectory(sysrap/tests)
    add_subdirectory(CSG/tests)
    add_subdirectory(qudarap/tests)
    add_subdirectory(CSGOptiX/tests)
    # u4/g4cx tests follow their packages; the top-level integration test
    # drives the simg4ox target from src and shells out through bash.
    if(NOT SIMPHONY_CORE_ONLY)
        add_subdirectory(u4/tests)
        add_subdirectory(g4cx/tests)
        add_subdirectory(tests)
    endif()
endif()

# Optional DD4hep integration plugins -- these link Geant4, so they follow
# the Geant4-dependent packages.
if(NOT SIMPHONY_CORE_ONLY)
    find_package(DD4hep QUIET COMPONENTS DDG4 DDCore)
endif()
if(DD4hep_FOUND)
    message(STATUS "DD4hep found -- building dd4hepplugins")
    add_subdirectory(dd4hepplugins)
    add_subdirectory(dd4hepplugins/examples)
else()
    message(STATUS "DD4hep not found -- skipping dd4hepplugins")
endif()

# Export configs
include(CMakePackageConfigHelpers)

# Generate the config file
configure_package_config_file(cmake/Config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

# Create and install version file
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
          ${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindOptiX.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

install(
    EXPORT ${PROJECT_NAME}Targets
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
    NAMESPACE ${PROJECT_NAME}::
)

install(
    TARGETS simphony_argparse
    EXPORT ${PROJECT_NAME}Targets
)

install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/third_party
)

install(
    DIRECTORY ./config
    DESTINATION ${GPHOX_INSTALL_FULL_DATADIR}
)
