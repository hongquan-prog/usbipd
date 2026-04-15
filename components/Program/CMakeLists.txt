# Program 组件 - 脱机烧录

cmake_minimum_required(VERSION 3.10)
set(COMPONENT_NAME program)

set(PROGRAM_C_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/swd_host.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hex_parser.c
)

set(COMPONENT_SRCS "")
foreach(src ${PROGRAM_C_SRCS})
    if(EXISTS ${src})
        list(APPEND COMPONENT_SRCS ${src})
    endif()
endforeach()

if(COMPONENT_SRCS)
    add_library(${COMPONENT_NAME} STATIC ${COMPONENT_SRCS})
    target_include_directories(${COMPONENT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)
    target_link_libraries(${COMPONENT_NAME} PUBLIC debug_probe)
    set_target_properties(${COMPONENT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    message(STATUS "${COMPONENT_NAME} library configured")
else()
    add_library(${COMPONENT_NAME} INTERFACE)
    target_include_directories(${COMPONENT_NAME} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
    message(STATUS "${COMPONENT_NAME} library: interface only")
endif()
