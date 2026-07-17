find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(BASH_EXECUTABLE bash REQUIRED)

set(SIMPHONY_SIMG4OX_TEST_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/simg4ox")
file(MAKE_DIRECTORY "${SIMPHONY_SIMG4OX_TEST_WORKDIR}")

add_test(
    NAME Integration.simg4ox
    COMMAND ${CMAKE_COMMAND} -E env
            REPO_DIR=${PROJECT_SOURCE_DIR}
            PYTHON=${Python3_EXECUTABLE}
            SIMG4OX_BIN=$<TARGET_FILE:simg4ox>
            ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_simg4ox.sh
)

set_tests_properties(Integration.simg4ox PROPERTIES
    WORKING_DIRECTORY "${SIMPHONY_SIMG4OX_TEST_WORKDIR}"
    LABELS "integration"
    TIMEOUT 120
)

set(SIMPHONY_SIMG4OX_MULTIEVENT_TEST_WORKDIR "${CMAKE_CURRENT_BINARY_DIR}/simg4ox_multievent")
file(MAKE_DIRECTORY "${SIMPHONY_SIMG4OX_MULTIEVENT_TEST_WORKDIR}")

add_test(
    NAME Integration.simg4ox_multievent
    COMMAND ${CMAKE_COMMAND} -E env
            REPO_DIR=${PROJECT_SOURCE_DIR}
            PYTHON=${Python3_EXECUTABLE}
            SIMG4OX_BIN=$<TARGET_FILE:simg4ox>
            ${BASH_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_simg4ox_multievent.sh
)

set_tests_properties(Integration.simg4ox_multievent PROPERTIES
    WORKING_DIRECTORY "${SIMPHONY_SIMG4OX_MULTIEVENT_TEST_WORKDIR}"
    LABELS "integration"
    TIMEOUT 120
)
