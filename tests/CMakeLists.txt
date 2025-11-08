# Tests CMakeLists.txt
# This file builds test executables for lwsip

# Set output directory for test executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

# Platform-specific library paths
if(APPLE)
    set(MEDIA_PLATFORM "debug.darwin")
elseif(UNIX)
    set(MEDIA_PLATFORM "debug.linux64")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# Library paths
set(LIB_SIP ${CMAKE_SOURCE_DIR}/3rds/media-server/libsip/${MEDIA_PLATFORM}/libsip.a)
set(LIB_SDK ${CMAKE_SOURCE_DIR}/3rds/sdk/libsdk/${MEDIA_PLATFORM}/libsdk.a)
set(LIB_HTTP ${CMAKE_SOURCE_DIR}/3rds/sdk/libhttp/${MEDIA_PLATFORM}/libhttp.a)

# Common include directories
set(TEST_INCLUDES
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/osal/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/libsip/include
    ${CMAKE_SOURCE_DIR}/3rds/sdk/include
    ${CMAKE_SOURCE_DIR}/3rds/sdk/libhttp/include
)

# Common compile flags
add_compile_options(-Wall -Wextra -O0 -g -D__LWS_PTHREAD__)

# ========================================
# 1. sip_fake - Simple SIP fake server
# ========================================
add_executable(sip_fake
    sip_fake.c
)

target_include_directories(sip_fake PRIVATE
    ${TEST_INCLUDES}
)

# No special libraries needed for sip_fake (uses only standard C)

# ========================================
# 2. lws_trans_test - Transport layer test
# ========================================
add_executable(lws_trans_test
    lws_trans_test.c
    ${CMAKE_SOURCE_DIR}/src/lwsip.c
    ${CMAKE_SOURCE_DIR}/src/lws_trans.c
    ${CMAKE_SOURCE_DIR}/src/lws_trans_udp.c
    ${CMAKE_SOURCE_DIR}/osal/src/macos/lws_mem.c
)

target_include_directories(lws_trans_test PRIVATE
    ${TEST_INCLUDES}
)

target_link_libraries(lws_trans_test
    pthread
)

# ========================================
# 3. caller - SIP caller test
# ========================================
add_executable(caller
    caller.c
    ${CMAKE_SOURCE_DIR}/src/lws_trans.c
    ${CMAKE_SOURCE_DIR}/src/lws_trans_udp.c
    ${CMAKE_SOURCE_DIR}/src/lws_agent.c
    ${CMAKE_SOURCE_DIR}/src/lws_timer.c
    ${CMAKE_SOURCE_DIR}/osal/src/macos/lws_mem.c
    ${CMAKE_SOURCE_DIR}/osal/src/macos/lws_mutex.c
    ${CMAKE_SOURCE_DIR}/osal/src/macos/lws_thread.c
)

target_include_directories(caller PRIVATE
    ${TEST_INCLUDES}
)

target_link_libraries(caller
    ${LIB_SIP}
    ${LIB_HTTP}
    ${LIB_SDK}
    pthread
)

# ========================================
# 4. callee - SIP callee test
# ========================================
add_executable(callee
    callee.c
    ${CMAKE_SOURCE_DIR}/src/lws_trans.c
    ${CMAKE_SOURCE_DIR}/src/lws_trans_udp.c
    ${CMAKE_SOURCE_DIR}/src/lws_agent.c
    ${CMAKE_SOURCE_DIR}/src/lws_timer.c
    ${CMAKE_SOURCE_DIR}/osal/src/macos/lws_mem.c
    ${CMAKE_SOURCE_DIR}/osal/src/macos/lws_mutex.c
    ${CMAKE_SOURCE_DIR}/osal/src/macos/lws_thread.c
)

target_include_directories(callee PRIVATE
    ${TEST_INCLUDES}
)

target_link_libraries(callee
    ${LIB_SIP}
    ${LIB_HTTP}
    ${LIB_SDK}
    pthread
)

# ========================================
# Optional: DEBUG_SIP test (commented out for now)
# ========================================
# option(DEBUG_SIP "Enable DEBUG_SIP test" OFF)
# if(DEBUG_SIP)
#     add_executable(lws_agent_test
#         lws_agent_test_main.c
#         ${CMAKE_SOURCE_DIR}/osal/src/macos/lws_mem.c
#         ${CMAKE_SOURCE_DIR}/src/lws_timer.c
#     )
#
#     target_include_directories(lws_agent_test PRIVATE
#         ${TEST_INCLUDES}
#     )
#
#     target_compile_definitions(lws_agent_test PRIVATE -DDEBUG_SIP)
#
#     target_link_libraries(lws_agent_test
#         ${LIB_SIP}
#         ${LIB_HTTP}
#         ${LIB_SDK}
#         pthread
#     )
# endif()

message(STATUS "Tests will be built to: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
