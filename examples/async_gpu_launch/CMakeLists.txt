cmake_minimum_required(VERSION 3.20)

project(async_gpu_launch LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(eic-opticks REQUIRED)
find_package(Geant4 REQUIRED)

add_executable(async_gpu_launch async_gpu_launch.cpp async_gpu_launch.h)
target_link_libraries(async_gpu_launch
    eic-opticks::gphox
    eic-opticks::G4CX
    eic-opticks::SysRap
    eic-opticks::U4
    ${Geant4_LIBRARIES}
)

install(TARGETS async_gpu_launch)
