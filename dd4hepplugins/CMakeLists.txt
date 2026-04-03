#------------------------------- -*- cmake -*- -------------------------------#
# eic-opticks DD4hep integration plugins
#
# Builds DD4hep action plugins that integrate eic-opticks GPU-accelerated
# optical photon simulation into DD4hep/Geant4.
#
# Works both as part of the top-level eic-opticks build (in-tree) and as a
# standalone project against an installed eic-opticks.
#
# Usage from a DD4hep steering file:
#   OpticsRun      -- initializes/finalizes G4CXOpticks per run
#   OpticsEvent    -- triggers GPU simulation per event
#----------------------------------------------------------------------------#

# Detect standalone vs in-tree build
if(NOT TARGET G4CX)
    cmake_minimum_required(VERSION 3.18 FATAL_ERROR)
    project(ddeicopticks VERSION 0.1.0 LANGUAGES CXX)
    find_package(DD4hep REQUIRED COMPONENTS DDG4 DDCore)
    find_package(eic-opticks REQUIRED)
    find_package(Geant4 REQUIRED)
    set(_g4cx   eic-opticks::G4CX)
    set(_u4     eic-opticks::U4)
    set(_sysrap eic-opticks::SysRap)
else()
    # In-tree: DD4hep already found by parent, targets use local names
    find_package(Geant4 REQUIRED)
    set(_g4cx   G4CX)
    set(_u4     U4)
    set(_sysrap SysRap)
endif()

dd4hep_set_compiler_flags()

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set(LIBRARY_OUTPUT_PATH "${CMAKE_CURRENT_BINARY_DIR}")

dd4hep_add_plugin(ddeicopticks
  SOURCES
    OpticsRun.cc
    OpticsEvent.cc
    OpticsSteppingAction.cc
  USES
    DD4hep::DDG4
    DD4hep::DDCore
    ${_g4cx}
    ${_u4}
    ${_sysrap}
)
target_compile_definitions(ddeicopticks PRIVATE STANDALONE)

install(TARGETS ddeicopticks
  LIBRARY DESTINATION lib
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_SHARED_MODULE_PREFIX}ddeicopticks.components
  DESTINATION lib
)
