cmake_minimum_required(VERSION 3.22)

project(simphox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(simphony REQUIRED)

add_executable(simphox simphox.cpp)
target_link_libraries(simphox simphony::SysRap)

install(TARGETS simphox)
