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

# the Geant4 reference mode links only Geant4: its simphony use is header-only
# (sysrap NP/sphoton, the qudarap Cu table), so take the include directories
# from the imported targets without linking their libraries. sphoton.h sits
# on the CUDA vector types, so the CUDA headers are needed at compile time.
find_package(CUDAToolkit REQUIRED)
add_executable(synrad_g4 synrad_g4.cpp)
get_target_property(_sysrap_inc  simphony::SysRap  INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(_qudarap_inc simphony::QUDARap INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(synrad_g4 PRIVATE ${_sysrap_inc} ${_qudarap_inc} ${CUDAToolkit_INCLUDE_DIRS})
target_link_libraries(synrad_g4 ${Geant4_LIBRARIES})

install(TARGETS synrad synrad_g4)
