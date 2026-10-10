find_package(Geant4 REQUIRED ui_all vis_all)

add_library(simphony SHARED
    api/simphony.cc
    api/simphony.h
    api/SIMPHONY_API_EXPORT.hh
)
add_library(simphony::simphony ALIAS simphony)

target_link_libraries(simphony PUBLIC U4 CSGOptiX)
target_include_directories(simphony PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
)

# consgeo is a tool for converting GDML into CSG
add_executable(consgeo consgeo.cc)

target_link_libraries(consgeo ${Geant4_LIBRARIES} simphony)

target_include_directories(consgeo PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)
target_link_libraries(consgeo simphony_argparse)

# simg4ox validates torch photons with serial/MT Geant4 and serialized OptiX launches
add_executable(simg4ox simg4ox.cpp g4app.h)
target_compile_definitions(simg4ox PRIVATE SIMPHONY_VERSION="${PROJECT_VERSION}")

target_link_libraries(simg4ox SysRap ${Geant4_LIBRARIES} simphony simphony_argparse)

target_include_directories(simg4ox PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUCerenkov - Cerenkov-only example with Opticks GPU raytracing
add_executable(GPUCerenkov GPUCerenkov.cpp GPUCerenkov.h)
target_link_libraries(GPUCerenkov SysRap ${Geant4_LIBRARIES} simphony simphony_argparse)
target_include_directories(GPUCerenkov PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPURaytrace example - Cerenkov + Scintillation with Opticks GPU raytracing
add_executable(GPURaytrace GPURaytrace.cpp GPURaytrace.h)
target_link_libraries(GPURaytrace SysRap ${Geant4_LIBRARIES} simphony simphony_argparse)
target_include_directories(GPURaytrace PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUPhotonSourceMinimal - GPU-only ray tracing (no G4 optical photon tracking)
add_executable(GPUPhotonSourceMinimal GPUPhotonSourceMinimal.cpp GPUPhotonSourceMinimal.h)
target_link_libraries(GPUPhotonSourceMinimal SysRap ${Geant4_LIBRARIES} simphony simphony_argparse)
target_include_directories(GPUPhotonSourceMinimal PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# GPUPhotonFileSource - reads photons from .npy file instead of torch generation
add_executable(GPUPhotonFileSource GPUPhotonFileSource.cpp GPUPhotonFileSource.h)
target_link_libraries(GPUPhotonFileSource SysRap ${Geant4_LIBRARIES} simphony simphony_argparse)
target_include_directories(GPUPhotonFileSource PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

# simtox creates a numpy file with initial photons for simulation
add_executable(simtox simtox.cpp)

target_link_libraries(simtox SysRap)

target_include_directories(simtox PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

install(FILES api/simphony.h api/SIMPHONY_API_EXPORT.hh
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
)

install(TARGETS simphony consgeo simg4ox GPUCerenkov GPURaytrace GPUPhotonSourceMinimal GPUPhotonFileSource simtox
    EXPORT ${PROJECT_NAME}Targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
)
