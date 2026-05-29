cmake_minimum_required(VERSION 3.20)

project(drich_gdml LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(eic-opticks REQUIRED)
find_package(Geant4 REQUIRED gdml)   # gdml component pulls G4GDMLParser + xerces-c

# Standalone (no-DD4hep) Geant4 + Opticks dRICH driver. Builds against an
# installed eic-opticks the same way as the sibling examples; compiling it
# serves as a build-smoke for the standalone GDML flow.
add_executable(drich_gdml_main drich_gdml_main.cc)
target_link_libraries(drich_gdml_main
    eic-opticks::gphox
    eic-opticks::G4CX
    eic-opticks::U4
    eic-opticks::QUDARap
    eic-opticks::CSG
    eic-opticks::CSGOptiX
    eic-opticks::SysRap
    ${Geant4_LIBRARIES}
)

install(TARGETS drich_gdml_main)
