cmake_minimum_required(VERSION 3.20)

project(GPURaytraceQuasi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Order matters: pull in glm before eic-opticks because the installed
# eic-opticks::SysRap target's link interface references glm::glm but the
# package's Config.cmake does not declare it as a dependency.
find_package(glm REQUIRED)
find_package(eic-opticks REQUIRED)
find_package(Geant4 REQUIRED ui_all vis_all)

add_executable(GPURaytraceQuasi GPURaytraceQuasi.cpp GPURaytraceQuasi.h)
target_link_libraries(GPURaytraceQuasi
    eic-opticks::G4CX
    eic-opticks::SysRap
    eic-opticks::U4
    ${Geant4_LIBRARIES}
)

install(TARGETS GPURaytraceQuasi)
