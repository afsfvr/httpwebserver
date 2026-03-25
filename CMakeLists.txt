cmake_minimum_required(VERSION 3.5)

project(main LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

string(ASCII 27 ESC)
set(RED "${ESC}[0;31m")
set(GREEN "${ESC}[0;32m")
set(YELLOW "${ESC}[1;33m")
set(BLUE "${ESC}[0;34m")
set(NOCOLOR "${ESC}[0m")

option(LOG "Enable log" ON)
option(REDIS "Enable redis" OFF)
option(HTTPS "Enable https" OFF)
option(NGINX "Enable nginx mode" OFF)
option(BUILD_WEBAPPS "Build all webapps modules" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

message(STATUS "CMake generator: ${GREEN}${CMAKE_GENERATOR}${NOCOLOR}")
message(STATUS "CMake build type: ${GREEN}${CMAKE_BUILD_TYPE}${NOCOLOR}")
if(CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Available configurations: ${GREEN}${CMAKE_CONFIGURATION_TYPES}${NOCOLOR}")
endif(CMAKE_CONFIGURATION_TYPES)

file(GLOB SRC *.cpp)
add_executable(${PROJECT_NAME} ${SRC})

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
    -Wall
    $<$<CONFIG:Debug>:-g -ggdb -O0>
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g -ggdb>
    $<$<CONFIG:MinSizeRel>:-Os>
    -fmacro-prefix-map=${PROJECT_SOURCE_DIR}/=
    -ffile-prefix-map=${PROJECT_SOURCE_DIR}/=
)

target_link_libraries(${PROJECT_NAME} pthread dl)

target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)

if(BUILD_WEBAPPS)
    add_subdirectory(webapps/root)
    add_subdirectory(webapps/upload)
endif(BUILD_WEBAPPS)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

add_custom_target(authorize
    COMMAND sudo setcap 'cap_net_bind_service=+ep' '${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}'
    DEPENDS ${PROJECT_NAME}
    )
