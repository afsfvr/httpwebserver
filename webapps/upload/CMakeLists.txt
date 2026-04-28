cmake_minimum_required(VERSION 3.5)

project(upload LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(LOG "Enable log" ON)
option(REDIS "Enable redis" OFF)
option(HTTPS "Enable https" OFF)
option(NGINX "Enable nginx mode" OFF)

file(GLOB SRC *.cpp)
add_library(${PROJECT_NAME} SHARED ${SRC})

if(NOT LOG)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NO_LOG)
endif(NOT LOG)
if(REDIS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_REDIS)
    find_library(HIREDIS_LIB hiredis)
    target_link_libraries(${PROJECT_NAME} ${HIREDIS_LIB})
endif(REDIS)
if(HTTPS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HTTPS)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} OpenSSL::SSL OpenSSL::Crypto)
endif(HTTPS)
if(NGINX)
    target_compile_options(${PROJECT_NAME} PRIVATE USE_NGINX)
endif(NGINX)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Release>:RELEASE;NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:RELEASE;NDEBUG>
    $<$<CONFIG:MinSizeRel>:RELEASE;NDEBUG>
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall # 常用警告
    -Wextra # 额外警告
    -Wpedantic # 标准兼容警告
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g>
    $<$<CONFIG:MinSizeRel>:-Os>
    -fmacro-prefix-map=${PROJECT_SOURCE_DIR}/=
    -ffile-prefix-map=${PROJECT_SOURCE_DIR}/=
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_NAME "main"
    POSITION_INDEPENDENT_CODE ON       # 必须，生成 PIC
)
