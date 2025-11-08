# CMakeLists.txt for lwsip tests
cmake_minimum_required(VERSION 3.10)

project(lwsip_tests C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Add compile options
add_compile_options(-Wall -Wextra)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../osal/include
    ${CMAKE_CURRENT_SOURCE_DIR}/unity_framework/src
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)

# Unity test framework
add_library(unity STATIC
    unity_framework/src/unity.c
)

# Mock libraries
add_library(mocks STATIC
    mocks/mock_sip_agent.c
)

# Unit tests
add_executable(test_transport_tcp
    unit/test_transport_tcp.c
    ../src/lws_transport_tcp.c
)

target_link_libraries(test_transport_tcp
    unity
    mocks
)

# Enable testing
enable_testing()

# Add tests to CTest
add_test(NAME test_transport_tcp COMMAND test_transport_tcp)

# Custom target to run all tests
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS test_transport_tcp
    COMMENT "Running all unit tests"
)

# Print test summary
add_custom_target(test_summary
    COMMAND echo "====================================="
    COMMAND echo "lwsip Unit Test Suite"
    COMMAND echo "====================================="
    COMMAND echo "Available tests:"
    COMMAND echo "  - test_transport_tcp: TCP transport layer tests"
    COMMAND echo ""
    COMMAND echo "Run 'make run_tests' to execute all tests"
    COMMAND echo "====================================="
)
