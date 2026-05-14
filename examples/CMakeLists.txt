cmake_minimum_required(VERSION 3.20)

project(simphox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(simphony REQUIRED)

add_executable(simphox simphox.cpp)
target_link_libraries(simphox simphony::gphox simphony::G4CX simphony::SysRap)

install(TARGETS simphox)
