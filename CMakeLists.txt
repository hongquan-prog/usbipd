cmake_minimum_required(VERSION 3.10)
project(usbip-server VERSION 0.2 LANGUAGES C)

#============================================================================
# 编译选项
#============================================================================

option(DEBUG "Enable debug output" OFF)
option(BUILD_STATIC_LIB "Build static library" OFF)

#============================================================================
# 配置生成 (Kconfig)
#============================================================================

# 运行配置生成脚本
execute_process(
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/gen_config.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CONFIG_GEN_RESULT
)

if(CONFIG_GEN_RESULT EQUAL 0)
    message(STATUS "Configuration generated successfully")
    # 包含生成的 CMake 配置
    include(${CMAKE_BINARY_DIR}/config.cmake)
else()
    message(WARNING "Failed to generate configuration")
endif()

#============================================================================
# 编译器设置
#============================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

#============================================================================
# 包含路径
#============================================================================

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/components/debug_probe
    ${CMAKE_SOURCE_DIR}/components/debug_probe/DAP/Include
    ${CMAKE_SOURCE_DIR}/components/debug_probe/DAP/Config
)

#============================================================================
# 源文件分组
#============================================================================

# 协议层
set(PROTO_SRCS
    src/server/usbip_protocol.c
)

# HAL 层
set(HAL_SRCS
    src/hal/usbip_osal.c
    src/hal/usbip_mempool.c
    src/hal/usbip_transport.c
)

# 服务器核心层
set(SERVER_SRCS
    src/server/usbip_server.c
    src/server/usbip_urb.c
    src/server/usbip_devmgr.c
    src/server/usbip_control.c
)

# 设备驱动层
set(DEVICE_SRCS
    src/device/usbip_hid.c
    src/device/usbip_bulk.c
)

# DAP 组件
set(DAP_SRCS
    components/debug_probe/DAP/Source/DAP.c
    components/debug_probe/DAP/Source/SW_DP.c
    components/debug_probe/DAP/Source/JTAG_DP.c
    components/debug_probe/DAP/Source/DAP_vendor.c
    components/debug_probe/debug_gpio.c
)

# 库源文件（不含主程序）
set(LIB_SRCS
    ${PROTO_SRCS}
    ${TRANS_SRCS}
    ${HAL_SRCS}
    ${SERVER_SRCS}
    ${DEVICE_SRCS}
    ${DAP_SRCS}
)

#============================================================================
# 可执行目标
#============================================================================

add_executable(${PROJECT_NAME}
    src/main.c
    src/osal_posix.c
    src/transport_tcp.c
    src/hid_dap.c
    src/bulk_dap.c
    ${LIB_SRCS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE pthread)

# 调试模式定义
if(DEBUG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG)
endif()

#============================================================================
# 静态库（可选）
#============================================================================

if(BUILD_STATIC_LIB)
    add_library(${PROJECT_NAME}-static STATIC ${LIB_SRCS})
    target_link_libraries(${PROJECT_NAME}-static PUBLIC pthread)
    set_target_properties(${PROJECT_NAME}-static PROPERTIES
        OUTPUT_NAME usbip-server
    )
endif()

#============================================================================
# 安装规则
#============================================================================

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
)

if(BUILD_STATIC_LIB)
    install(TARGETS ${PROJECT_NAME}-static
        ARCHIVE DESTINATION lib
    )
    install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/
        DESTINATION include/usbip-server
    )
endif()

#============================================================================
# 测试
#============================================================================

enable_testing()

add_test(NAME usbip-help
    COMMAND ${PROJECT_NAME} -h
)

#============================================================================
# 打包
#============================================================================

set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "USBIP Server with virtual device support")
set(CPACK_GENERATOR "TGZ;DEB")
include(CPack)