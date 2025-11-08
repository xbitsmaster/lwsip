cmake_minimum_required(VERSION 3.10)
project(lwsip C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Ensure out-of-source build
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please create a build directory and run cmake from there.")
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================
# Feature Options
# ============================================================
option(ENABLE_MQTT "Enable MQTT transport support" OFF)
option(ENABLE_FILE "Enable file-based media backend" ON)
option(ENABLE_MEMORY "Enable memory-based media backend" OFF)
option(ENABLE_DEVICE "Enable audio/video device backend" OFF)

message(STATUS "Feature configuration:")
message(STATUS "  MQTT transport: ${ENABLE_MQTT}")
message(STATUS "  File backend: ${ENABLE_FILE}")
message(STATUS "  Memory backend: ${ENABLE_MEMORY}")
message(STATUS "  Device backend: ${ENABLE_DEVICE}")

# Platform-specific settings for media-server libraries
if(APPLE)
    set(MEDIA_PLATFORM "debug.darwin")
elseif(UNIX)
    set(MEDIA_PLATFORM "debug.linux64")
elseif(WIN32)
    set(MEDIA_PLATFORM "debug.win64")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# ============================================================
# Include Directories
# ============================================================
set(COMMON_INCLUDES
    # lwsip headers
    ${CMAKE_SOURCE_DIR}/include

    # OSAL headers
    ${CMAKE_SOURCE_DIR}/osal/include

    # SDK headers
    ${CMAKE_SOURCE_DIR}/3rds/sdk/include
    ${CMAKE_SOURCE_DIR}/3rds/sdk/libhttp/include
    ${CMAKE_SOURCE_DIR}/3rds/sdk/libaio/include
    ${CMAKE_SOURCE_DIR}/3rds/sdk/libice/include

    # media-server headers
    ${CMAKE_SOURCE_DIR}/3rds/media-server/libsip/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/librtp/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/librtsp/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/libmpeg/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/libmov/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/libflv/include
)

# ============================================================
# Library Paths
# ============================================================
set(OSAL_LIBS ${CMAKE_SOURCE_DIR}/osal/build/lib/liblwsosal.a)
set(LIB_SIP ${CMAKE_SOURCE_DIR}/3rds/media-server/libsip/${MEDIA_PLATFORM}/libsip.a)
set(LIB_RTP ${CMAKE_SOURCE_DIR}/3rds/media-server/librtp/${MEDIA_PLATFORM}/librtp.a)
set(LIB_RTSP ${CMAKE_SOURCE_DIR}/3rds/media-server/librtsp/${MEDIA_PLATFORM}/librtsp.a)
set(LIB_MPEG ${CMAKE_SOURCE_DIR}/3rds/media-server/libmpeg/${MEDIA_PLATFORM}/libmpeg.a)
set(LIB_MOV ${CMAKE_SOURCE_DIR}/3rds/media-server/libmov/${MEDIA_PLATFORM}/libmov.a)
set(LIB_FLV ${CMAKE_SOURCE_DIR}/3rds/media-server/libflv/${MEDIA_PLATFORM}/libflv.a)
set(LIB_HTTP ${CMAKE_SOURCE_DIR}/3rds/sdk/libhttp/${MEDIA_PLATFORM}/libhttp.a)
set(LIB_AIO ${CMAKE_SOURCE_DIR}/3rds/sdk/libaio/${MEDIA_PLATFORM}/libaio.a)
set(LIB_ICE ${CMAKE_SOURCE_DIR}/3rds/sdk/libice/${MEDIA_PLATFORM}/libice.a)
set(LIB_SDK ${CMAKE_SOURCE_DIR}/3rds/sdk/libsdk/${MEDIA_PLATFORM}/libsdk.a)

# Platform-specific libraries
if(APPLE)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    set(PLATFORM_LIBS ${AUDIOTOOLBOX_FRAMEWORK})
elseif(UNIX)
    # Linux: ALSA
    find_package(ALSA)
    if(ALSA_FOUND)
        set(PLATFORM_LIBS ${ALSA_LIBRARIES})
        include_directories(${ALSA_INCLUDE_DIRS})
    endif()
endif()

# Common libraries to link
set(COMMON_LIBS
    ${LIB_SIP}
    ${LIB_RTP}
    ${LIB_RTSP}
    ${LIB_MPEG}
    ${LIB_MOV}
    ${LIB_FLV}
    ${LIB_HTTP}
    # ${LIB_AIO}   # TODO: Fix libaio compilation issues (optional)
    ${LIB_ICE}
    ${LIB_SDK}
    ${OSAL_LIBS}
    ${PLATFORM_LIBS}
    pthread
)

# ============================================================
# lwsip Library
# ============================================================
set(LWS_SOURCES
    src/lws_agent.c
    src/lws_trans.c
    src/lws_trans_udp.c
    src/lws_sess.c
    src/lws_dev.c
    src/lws_timer.c
)

# Optional: file-based device backend
if(ENABLE_FILE)
    list(APPEND LWS_SOURCES src/lws_dev_file.c)
endif()

# Optional: MQTT transport
if(ENABLE_MQTT)
    # TODO: Add MQTT transport source when implemented
    # list(APPEND LWS_SOURCES src/lws_trans_mqtt.c)
endif()

# Platform-specific device backends
if(APPLE)
    list(APPEND LWS_SOURCES src/lws_dev_macos.c)
elseif(UNIX)
    list(APPEND LWS_SOURCES src/lws_dev_linux.c)
endif()

# Optional: embedded device stub
option(ENABLE_DEV_STUB "Enable device stub for embedded systems" OFF)
if(ENABLE_DEV_STUB)
    list(APPEND LWS_SOURCES src/lws_dev_stub.c)
endif()

add_library(lwsip_static STATIC ${LWS_SOURCES})
set_target_properties(lwsip_static PROPERTIES OUTPUT_NAME "lwsip")

target_include_directories(lwsip_static PUBLIC
    ${COMMON_INCLUDES}
)

# Base compile definitions
target_compile_definitions(lwsip_static PRIVATE __LWS_PTHREAD__ LWS_ENABLE_LOG DEBUG)

# Feature-based compile definitions
if(ENABLE_MQTT)
    target_compile_definitions(lwsip_static PUBLIC TRANS_MQTT)
endif()

if(ENABLE_FILE)
    target_compile_definitions(lwsip_static PUBLIC DEV_FILE)
endif()

if(ENABLE_MEMORY)
    target_compile_definitions(lwsip_static PUBLIC LWS_ENABLE_MEDIA_MEMORY)
endif()

if(ENABLE_DEVICE)
    target_compile_definitions(lwsip_static PUBLIC LWS_ENABLE_MEDIA_DEVICE)
endif()

target_link_libraries(lwsip_static
    ${COMMON_LIBS}
)

# ============================================================
# lwsip-cli Executable
# ============================================================
add_subdirectory(cmd)

# ============================================================
# Tests
# ============================================================
add_subdirectory(tests)

# ============================================================
# Installation
# ============================================================
install(TARGETS lwsip_static DESTINATION lib)
# install(TARGETS lwsip-cli DESTINATION bin)  # Commented out - lwsip_cli.c has been deleted
install(DIRECTORY include/ DESTINATION include/lwsip
    FILES_MATCHING PATTERN "*.h")
