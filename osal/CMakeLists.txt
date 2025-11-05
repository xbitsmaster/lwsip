cmake_minimum_required(VERSION 3.10)
project(lwsosal C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Thread platform selection
# Usage: cmake -DTHREAD=pthread (default)
#        cmake -DTHREAD=freertos
#        cmake -DTHREAD=zephyr
if(NOT DEFINED THREAD)
    set(THREAD "pthread")
endif()

message(STATUS "OSAL Thread Platform: ${THREAD}")

# Detect platform if using pthread
if(THREAD STREQUAL "pthread")
    set(PLATFORM_MACRO "__LWS_PTHREAD__")

    # Auto-detect OS
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(PLATFORM_DIR "linux")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(PLATFORM_DIR "macos")
    else()
        message(FATAL_ERROR "Unsupported OS for pthread: ${CMAKE_SYSTEM_NAME}")
    endif()

elseif(THREAD STREQUAL "freertos")
    set(PLATFORM_MACRO "__LWS_FREERTOS__")
    set(PLATFORM_DIR "freertos")

elseif(THREAD STREQUAL "zephyr")
    set(PLATFORM_MACRO "__LWS_ZEPHYR__")
    set(PLATFORM_DIR "zephyr")

elseif(THREAD STREQUAL "rtthread")
    set(PLATFORM_MACRO "__LWS_RTTHREAD__")
    set(PLATFORM_DIR "rtthread")

else()
    message(FATAL_ERROR "Unknown THREAD platform: ${THREAD}")
endif()

message(STATUS "Platform directory: src/${PLATFORM_DIR}")
message(STATUS "Platform macro: ${PLATFORM_MACRO}")

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files from platform-specific directory
file(GLOB SOURCES "src/${PLATFORM_DIR}/*.c")

if(NOT SOURCES)
    message(FATAL_ERROR "No source files found in src/${PLATFORM_DIR}/")
endif()

# Create static library
add_library(lwsosal STATIC ${SOURCES})

# Add platform-specific compile definitions
target_compile_definitions(lwsosal PRIVATE ${PLATFORM_MACRO})

# Link pthread for pthread-based platforms
if(THREAD STREQUAL "pthread")
    find_package(Threads REQUIRED)
    target_link_libraries(lwsosal PRIVATE Threads::Threads)
endif()

# Set output directory
set_target_properties(lwsosal PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Installation
install(TARGETS lwsosal
    ARCHIVE DESTINATION lib
)
install(DIRECTORY include/
    DESTINATION include/lwsip
    FILES_MATCHING PATTERN "*.h"
)

# Display summary
message(STATUS "")
message(STATUS "=== OSAL Configuration Summary ===")
message(STATUS "Thread Platform: ${THREAD}")
message(STATUS "Platform Macro: ${PLATFORM_MACRO}")
message(STATUS "Source Directory: src/${PLATFORM_DIR}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "==================================")
