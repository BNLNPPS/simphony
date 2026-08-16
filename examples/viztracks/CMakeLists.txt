find_package(Geant4 REQUIRED ui_all vis_all)

add_executable(viztracks viztracks.cpp)
target_link_libraries(viztracks gphox ${Geant4_LIBRARIES} G4CX)
target_include_directories(viztracks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

install(TARGETS viztracks RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
