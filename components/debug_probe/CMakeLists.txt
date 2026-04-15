# debug_probe 组件 - CMSIS-DAP 调试探针驱动

cmake_minimum_required(VERSION 3.10)
set(COMPONENT_NAME debug_probe)

set(COMPONENT_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/DAP/Source/DAP.c
    ${CMAKE_CURRENT_SOURCE_DIR}/DAP/Source/SW_DP.c
    ${CMAKE_CURRENT_SOURCE_DIR}/DAP/Source/JTAG_DP.c
    ${CMAKE_CURRENT_SOURCE_DIR}/DAP/Source/DAP_vendor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/debug_gpio.c
)

add_library(${COMPONENT_NAME} STATIC ${COMPONENT_SRCS})

target_include_directories(${COMPONENT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/DAP/Include
        ${CMAKE_CURRENT_SOURCE_DIR}/DAP/Config
)

set_target_properties(${COMPONENT_NAME} PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)

message(STATUS "${COMPONENT_NAME} library configured")
