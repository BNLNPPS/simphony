cmake_minimum_required(VERSION 3.22)

project(synrad LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Order matters: pull in glm before simphony because the installed
# simphony::SysRap target's link interface references glm::glm but the
# package's Config.cmake does not declare it as a dependency.
find_package(glm REQUIRED)
find_package(simphony REQUIRED)
find_package(Geant4 REQUIRED)

add_executable(synrad synrad.cpp)
target_link_libraries(synrad
    simphony::G4CX
    simphony::QUDARap
    simphony::SysRap
    ${Geant4_LIBRARIES}
)

install(TARGETS synrad)
