find_package(Geant4 REQUIRED ui_all vis_all)

add_executable(StandAloneGeant4Validation
    StandAloneGeant4Validation.cpp
    StandAloneGeant4Validation.h
)
target_link_libraries(StandAloneGeant4Validation gphox ${Geant4_LIBRARIES} U4)
target_compile_definitions(StandAloneGeant4Validation PRIVATE WITH_INSTRUMENTED_DEBUG)
target_include_directories(StandAloneGeant4Validation PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
)

install(TARGETS StandAloneGeant4Validation RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
