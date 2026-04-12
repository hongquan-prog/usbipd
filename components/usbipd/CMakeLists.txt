# usbipd 组件 - USBIP 服务器核心

cmake_minimum_required(VERSION 3.10)
set(COMPONENT_NAME usbipd)

set(COMPONENT_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/usbip_server.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/usbip_conn.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/usbip_urb.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/usbip_devmgr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/usbip_control.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server/usbip_pack.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/device/usbip_hid.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/device/usbip_bulk.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/usbip_osal.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/usbip_mempool.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/usbip_transport.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/posix/osal_posix.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/posix/transport_tcp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hid_dap.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bulk_dap.c
)

add_library(${COMPONENT_NAME} STATIC ${COMPONENT_SRCS})

target_include_directories(${COMPONENT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(${COMPONENT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/priv
)

target_link_libraries(${COMPONENT_NAME} PUBLIC debug_probe)

# Force include constructor symbols when linking this static library
# These are __attribute__((constructor)) functions that would otherwise be discarded
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(${COMPONENT_NAME} INTERFACE
        -Wl,-u,osal_register_posix
        -Wl,-u,transport_register_tcp
        -Wl,-u,hid_dap_driver_register
        -Wl,-u,bulk_dap_driver_register
    )
endif()

set_target_properties(${COMPONENT_NAME} PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)

message(STATUS "${COMPONENT_NAME} library configured")
