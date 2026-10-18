#------------------------------- -*- cmake -*- -------------------------------#
# simphony DD4hep integration plugins
#
# Builds DD4hep action plugins that integrate simphony GPU-accelerated
# optical photon simulation into DD4hep/Geant4.
#
# Works both as part of the top-level simphony build (in-tree) and as a
# standalone project against an installed simphony.
#
# Usage from a DD4hep steering file:
#   OpticsRun      -- initializes/reuses the process-wide Simphony runtime
#   OpticsEvent    -- triggers GPU simulation per event
#----------------------------------------------------------------------------#

# Detect standalone vs in-tree build
if(NOT TARGET simphony)
    cmake_minimum_required(VERSION 3.22 FATAL_ERROR)
    project(ddsimphony VERSION 0.1.0 LANGUAGES CXX)
    set(_simphony_standalone TRUE)
    find_package(DD4hep REQUIRED COMPONENTS DDG4 DDCore)
    find_package(simphony REQUIRED)
    find_package(Geant4 REQUIRED)
    set(_simphony simphony::simphony)
    set(_u4     simphony::U4)
    set(_sysrap simphony::SysRap)
else()
    # In-tree: DD4hep already found by parent, targets use local names
    find_package(Geant4 REQUIRED)
    set(_simphony simphony::simphony)
    set(_u4     U4)
    set(_sysrap SysRap)
endif()

# DD4hep copies Geant4's build flags into this helper target, including a
# hard-coded -std=c++17. Let CMake's compile features select the standard for
# consumers, which need C++20 for Simphony's event headers.
get_target_property(_geant4_options Geant4::Interface INTERFACE_COMPILE_OPTIONS)
if(_geant4_options)
    list(FILTER _geant4_options EXCLUDE REGEX "^(-std=|/std:)")
    set_property(TARGET Geant4::Interface PROPERTY
        INTERFACE_COMPILE_OPTIONS "${_geant4_options}")
endif()

dd4hep_set_compiler_flags()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(LIBRARY_OUTPUT_PATH "${CMAKE_CURRENT_BINARY_DIR}")

dd4hep_add_plugin(ddsimphony
  SOURCES
    OpticsRun.cc
    OpticsEvent.cc
    OpticsSteppingAction.cc
  USES
    DD4hep::DDG4
    DD4hep::DDCore
    ${_simphony}
    ${_u4}
    ${_sysrap}
)
target_compile_features(ddsimphony PRIVATE cxx_std_20)

# STANDALONE changes class export macros in simphony headers;
# only needed when building outside the main simphony tree.
if(_simphony_standalone)
    target_compile_definitions(ddsimphony PRIVATE STANDALONE)
endif()

install(TARGETS ddsimphony
  LIBRARY DESTINATION lib
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_SHARED_MODULE_PREFIX}ddsimphony.components
  DESTINATION lib
)
