cmake_minimum_required(VERSION 3.20)

project(MC_Truth_GPURaytrace LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(glm REQUIRED)
find_package(eic-opticks REQUIRED)
find_package(Geant4 REQUIRED ui_all vis_all)

add_executable(MC_Truth_GPURaytrace GPURaytrace.cpp GPURaytrace.h)
target_link_libraries(MC_Truth_GPURaytrace
    eic-opticks::G4CX
    eic-opticks::SysRap
    eic-opticks::U4
    ${Geant4_LIBRARIES}
)

install(TARGETS MC_Truth_GPURaytrace)
