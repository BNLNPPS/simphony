find_package(Geant4 REQUIRED ui_all vis_all)

# Common Geant4 and G4CX dependencies shared by executables using Geant4/G4CX
add_library(simphony_g4_deps INTERFACE)
target_link_libraries(simphony_g4_deps INTERFACE ${Geant4_LIBRARIES} G4CX)

# consgeo is a tool for converting GDML into CSG
add_executable(consgeo consgeo.cc)

target_link_libraries(consgeo simphony_g4_deps)

target_include_directories(consgeo PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)
target_link_libraries(consgeo simphony_argparse)

# simg4ox runs Geant4 and OptiX simulations
add_executable(simg4ox simg4ox.cpp g4app.h)

target_link_libraries(simg4ox SysRap simphony_g4_deps simphony_argparse)

target_include_directories(simg4ox PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUCerenkov - Cerenkov-only example with Opticks GPU raytracing
add_executable(GPUCerenkov GPUCerenkov.cpp GPUCerenkov.h)
target_link_libraries(GPUCerenkov SysRap simphony_g4_deps simphony_argparse)
target_include_directories(GPUCerenkov PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPURaytrace example - Cerenkov + Scintillation with Opticks GPU raytracing
add_executable(GPURaytrace GPURaytrace.cpp GPURaytrace.h)
target_link_libraries(GPURaytrace SysRap simphony_g4_deps simphony_argparse)
target_include_directories(GPURaytrace PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUPhotonSourceMinimal - GPU-only ray tracing (no G4 optical photon tracking)
add_executable(GPUPhotonSourceMinimal GPUPhotonSourceMinimal.cpp GPUPhotonSourceMinimal.h)
target_link_libraries(GPUPhotonSourceMinimal SysRap simphony_g4_deps simphony_argparse)
target_include_directories(GPUPhotonSourceMinimal PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUPhotonFileSource - reads photons from .npy file instead of torch generation
add_executable(GPUPhotonFileSource GPUPhotonFileSource.cpp GPUPhotonFileSource.h)
target_link_libraries(GPUPhotonFileSource SysRap simphony_g4_deps simphony_argparse)
target_include_directories(GPUPhotonFileSource PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# simtox creates a numpy file with initial photons for simulation
add_executable(simtox simtox.cpp)

target_link_libraries(simtox SysRap)

target_include_directories(simtox PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

install(TARGETS consgeo simg4ox GPUCerenkov GPURaytrace GPUPhotonSourceMinimal GPUPhotonFileSource simtox simphony_g4_deps
    EXPORT ${PROJECT_NAME}Targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
