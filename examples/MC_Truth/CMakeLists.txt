cmake_minimum_required(VERSION 3.20)

project(GPUMCTruth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(glm REQUIRED)
find_package(simphony REQUIRED)
find_package(Geant4 REQUIRED ui_all vis_all)

add_executable(GPUMCTruth GPUMCTruth.cpp GPUMCTruth.h)
target_include_directories(GPUMCTruth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
target_link_libraries(GPUMCTruth
    simphony::G4CX
    simphony::SysRap
    simphony::U4
    ${Geant4_LIBRARIES}
)

install(TARGETS GPUMCTruth)
