cmake_minimum_required(VERSION 3.10)

option(ENABLE_PROGRAM "Enable offline programming support" ON)
option(ENABLE_GDB_SERVER "Enable GDB server" ON)

if(ENABLE_PROGRAM OR ENABLE_GDB_SERVER)
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

if(ENABLE_PROGRAM OR ENABLE_GDB_SERVER)
    set(CMAKE_CXX_STANDARD 11)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wunused-function -Wunused-variable -Wunused-parameter)
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -g -O0)
    add_link_options(-fsanitize=address)
endif()

# 组件子目录
add_subdirectory(components/debug_probe)
add_subdirectory(components/usbipd)

if(ENABLE_PROGRAM)
    add_subdirectory(components/Program)
    add_definitions(-DENABLE_PROGRAM=1)
endif()

if(ENABLE_GDB_SERVER)
    add_subdirectory(components/gdb_server)
    add_definitions(-DENABLE_GDB_SERVER=1)
    include_directories(components/gdb_server/include)
endif()

# 可执行目标
add_executable(${PROJECT_NAME} src/main.c)

target_link_libraries(${PROJECT_NAME} PRIVATE pthread usbipd)

# Enable garbage collection of unused sections to reduce binary size
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--gc-sections)
endif()

if(ENABLE_PROGRAM)
    target_link_libraries(${PROJECT_NAME} PRIVATE program)
endif()

if(ENABLE_GDB_SERVER)
    target_link_libraries(${PROJECT_NAME} PRIVATE gdb_server)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE debug_probe)

if(ENABLE_PROGRAM OR ENABLE_GDB_SERVER)
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

if(ENABLE_GDB_SERVER)
    install(TARGETS gdb_server ARCHIVE DESTINATION lib)
endif()
