cmake_minimum_required(VERSION 3.10)

option(ENABLE_PROGRAM "Enable offline programming support" ON)

if(ENABLE_PROGRAM)
    project(usbip-server VERSION 0.2 LANGUAGES C CXX)
else()
    project(usbip-server VERSION 0.2 LANGUAGES C)
endif()

option(DEBUG "Enable debug output" OFF)
option(BUILD_STATIC_LIB "Build static library" OFF)

# 配置生成
execute_process(
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/gen_config.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE CONFIG_GEN_RESULT
)
if(CONFIG_GEN_RESULT EQUAL 0)
    include(${CMAKE_BINARY_DIR}/config.cmake)
endif()

# 编译器设置
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(ENABLE_PROGRAM)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# 组件子目录
add_subdirectory(components/debug_probe)
add_subdirectory(components/usbipd)

if(ENABLE_PROGRAM)
    add_subdirectory(components/Program)
    add_definitions(-DENABLE_PROGRAM=1)
endif()

# 可执行目标
add_executable(${PROJECT_NAME} src/main.c)

target_link_libraries(${PROJECT_NAME} PRIVATE pthread usbipd)

if(ENABLE_PROGRAM)
    target_link_libraries(${PROJECT_NAME} PRIVATE program)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE debug_probe)

if(ENABLE_PROGRAM)
    target_link_libraries(${PROJECT_NAME} PRIVATE stdc++)
endif()

if(DEBUG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG)
endif()

# 安装
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
install(TARGETS debug_probe usbipd ARCHIVE DESTINATION lib)

if(ENABLE_PROGRAM)
    install(TARGETS program ARCHIVE DESTINATION lib)
endif()
