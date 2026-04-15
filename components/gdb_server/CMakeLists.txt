# gdb_server 组件 - GDB Remote Serial Protocol Server

cmake_minimum_required(VERSION 3.10)

set(COMPONENT_NAME gdb_server)
set(BMDIR ${CMAKE_CURRENT_SOURCE_DIR}/blackmagic)

# 核心源文件
add_library(${COMPONENT_NAME} STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gdb_stub.c
)

# 包含目录
target_include_directories(${COMPONENT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${BMDIR}/src/include
        ${BMDIR}/src/platforms/hosted
        ${BMDIR}/src/target
    PRIVATE
        ${BMDIR}/src
)

# 链接Program组件以使用swd_host.c中的函数
target_link_libraries(${COMPONENT_NAME}
    PUBLIC
        debug_probe
        program
)

# 编译定义
target_compile_definitions(${COMPONENT_NAME}
    PRIVATE
        PC_HOSTED=1
        CONFIG_BMDA=1
)

set_target_properties(${COMPONENT_NAME} PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
)

message(STATUS "${COMPONENT_NAME} library configured")
