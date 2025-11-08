# lwsip CLI application
add_executable(lwsip-cli
    lwsip-cli.c
)

target_include_directories(lwsip-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/osal/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/libsip/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/librtp/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/libmov/include
    ${CMAKE_SOURCE_DIR}/3rds/sdk/include
    ${CMAKE_SOURCE_DIR}/3rds/sdk/libice/include
)

target_link_libraries(lwsip-cli
    lwsip_static
)

# Add CoreFoundation framework on macOS for audio device support
if(APPLE)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    if(COREFOUNDATION_FRAMEWORK)
        target_link_libraries(lwsip-cli ${COREFOUNDATION_FRAMEWORK})
    endif()
endif()

install(TARGETS lwsip-cli
    RUNTIME DESTINATION bin
)

# lwsip media test application
# Commented out until lwsip_media_test.c is created

#add_executable(lwsip-media-test
#    lwsip_media_test.c
#)
#
#target_include_directories(lwsip-media-test PRIVATE
#    ${CMAKE_CURRENT_SOURCE_DIR}/..
#    ${CMAKE_CURRENT_SOURCE_DIR}/../osal/include
#)
#
#target_link_libraries(lwsip-media-test
#    lwsip_static
#)
#
#install(TARGETS lwsip-media-test
#    RUNTIME DESTINATION bin
#)
