cmake_minimum_required(VERSION 3.18)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 75)
endif()

# Default CMAKE_BUILD_TYPE=Debug for single-config generators (Ninja/Makefiles) when not specified
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type")
endif()

project(eic-opticks VERSION 0.3.0 LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BUILD_SHARED_LIBS ON)

include(GNUInstallDirs)
include(CTest)

# used by src/config.h.in
set(GPHOX_INSTALL_FULL_DATADIR "${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}")

add_subdirectory(sysrap)
add_subdirectory(sysrap/tests)
add_subdirectory(CSG)
add_subdirectory(CSG/tests)
add_subdirectory(qudarap)
add_subdirectory(qudarap/tests)
add_subdirectory(CSGOptiX)
add_subdirectory(CSGOptiX/tests)
add_subdirectory(gdxml)
add_subdirectory(gdxml/tests)
add_subdirectory(u4)
add_subdirectory(u4/tests)
add_subdirectory(g4cx)
add_subdirectory(g4cx/tests)
add_subdirectory(src)

# Optional DD4hep integration plugins
find_package(DD4hep QUIET COMPONENTS DDG4 DDCore)
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
    DIRECTORY ./config
    DESTINATION ${GPHOX_INSTALL_FULL_DATADIR}
)
