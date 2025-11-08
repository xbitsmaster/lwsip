# lwsip CLI application

add_executable(lwsip-cli
    lwsip_cli.c
)

target_include_directories(lwsip-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../osal/include
)

target_link_libraries(lwsip-cli
    lwsip_static
)

install(TARGETS lwsip-cli
    RUNTIME DESTINATION bin
)

# lwsip media test application

add_executable(lwsip-media-test
    lwsip_media_test.c
)

target_include_directories(lwsip-media-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../osal/include
)

target_link_libraries(lwsip-media-test
    lwsip_static
)

install(TARGETS lwsip-media-test
    RUNTIME DESTINATION bin
)
