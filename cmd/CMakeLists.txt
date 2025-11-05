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
