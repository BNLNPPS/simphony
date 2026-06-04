find_package(Geant4 REQUIRED ui_all vis_all)

configure_file(config_path.h.in config_path.h @ONLY)

# gphox is the primary library
add_library(gphox config.cpp torch.cpp)
target_link_libraries(gphox PUBLIC SysRap)
target_include_directories(gphox
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

# Common Geant4 and G4CX dependencies shared by executables using Geant4/G4CX
add_library(gphox_g4_deps INTERFACE)
target_link_libraries(gphox_g4_deps INTERFACE ${Geant4_LIBRARIES} G4CX)

# consgeo is a tool for converting GDML into CSG
add_executable(consgeo consgeo.cc)

target_link_libraries(consgeo gphox_g4_deps)

target_include_directories(consgeo PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# simg4ox runs Geant4 and OptiX simulations
add_executable(simg4ox simg4ox.cpp g4app.h)

target_link_libraries(simg4ox gphox gphox_g4_deps)

target_include_directories(simg4ox PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUCerenkov - Cerenkov-only example with Opticks GPU raytracing
add_executable(GPUCerenkov GPUCerenkov.cpp GPUCerenkov.h)
target_link_libraries(GPUCerenkov gphox gphox_g4_deps)
target_include_directories(GPUCerenkov PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPURaytrace example - Cerenkov + Scintillation with Opticks GPU raytracing
add_executable(GPURaytrace GPURaytrace.cpp GPURaytrace.h)
target_link_libraries(GPURaytrace gphox gphox_g4_deps)
target_include_directories(GPURaytrace PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUPhotonSource - optical photon input via SetInputPhoton with Opticks GPU simulation
add_executable(GPUPhotonSource GPUPhotonSource.cpp GPUPhotonSource.h)
target_link_libraries(GPUPhotonSource gphox gphox_g4_deps)
target_include_directories(GPUPhotonSource PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUPhotonSourceMinimal - GPU-only ray tracing (no G4 optical photon tracking)
add_executable(GPUPhotonSourceMinimal GPUPhotonSourceMinimal.cpp GPUPhotonSourceMinimal.h)
target_link_libraries(GPUPhotonSourceMinimal gphox gphox_g4_deps)
target_include_directories(GPUPhotonSourceMinimal PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUPhotonFileSource - reads photons from .npy file instead of torch generation
add_executable(GPUPhotonFileSource GPUPhotonFileSource.cpp GPUPhotonFileSource.h)
target_link_libraries(GPUPhotonFileSource gphox gphox_g4_deps)
target_include_directories(GPUPhotonFileSource PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# StandAloneGeant4Validation - pure G4 optical photon simulation (no opticks GPU)
# Links U4 for aligned mode (U4Random, InstrumentedG4OpBoundaryProcess, ShimG4Op*)
add_executable(StandAloneGeant4Validation StandAloneGeant4Validation.cpp StandAloneGeant4Validation.h)
target_link_libraries(StandAloneGeant4Validation gphox gphox_g4_deps U4)
target_compile_definitions(StandAloneGeant4Validation PRIVATE WITH_INSTRUMENTED_DEBUG)
target_include_directories(StandAloneGeant4Validation PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# G4ValidationGenstep - pure G4 electron→scintillation/Cerenkov→optical photon simulation
add_executable(G4ValidationGenstep G4ValidationGenstep.cpp G4ValidationGenstep.h)
target_link_libraries(G4ValidationGenstep gphox gphox_g4_deps)
target_include_directories(G4ValidationGenstep PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# simtox creates a numpy file with initial photons for simulation
add_executable(simtox simtox.cpp)

target_link_libraries(simtox gphox)

target_include_directories(simtox PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

install(TARGETS consgeo simg4ox GPUCerenkov GPURaytrace GPUPhotonSource GPUPhotonSourceMinimal GPUPhotonFileSource StandAloneGeant4Validation G4ValidationGenstep simtox gphox gphox_g4_deps
    EXPORT ${PROJECT_NAME}Targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES config.h torch.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
