cmake_minimum_required(VERSION 3.22)

project(async_gpu_std LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(simphony REQUIRED)
find_package(Geant4 REQUIRED ui_all vis_all)
find_package(Threads REQUIRED)

add_executable(async_gpu_std async_gpu_std.cpp async_gpu_std.h)
target_link_libraries(async_gpu_std
    simphony::simphony
    simphony::SysRap
    simphony::U4
    ${Geant4_LIBRARIES}
    Threads::Threads
)

install(TARGETS async_gpu_std)
