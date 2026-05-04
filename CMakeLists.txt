cmake_minimum_required(VERSION 4.0)

find_package(Git REQUIRED)

set(_version "0.0.0")
execute_process(
    COMMAND ${GIT_EXECUTABLE} tag --list --sort=-version:refname
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE _git_tags
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
string(REPLACE "\n" ";" _git_tags "${_git_tags}")
foreach(_git_tag IN LISTS _git_tags)
    if(_git_tag MATCHES "^v?[0-9]+\.[0-9]+\.[0-9]+")
        string(
            REGEX MATCH "^v?[0-9]+\.[0-9]+\.[0-9]+"
            _version_prefix
            "${_git_tag}"
        )
        string(REGEX REPLACE "^v" "" _version "${_version_prefix}")
        break()
    endif()
endforeach()

project(reallm VERSION ${_version} LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(
    EXISTS "${CMAKE_SOURCE_DIR}/.git"
    AND IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.githooks"
)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} config core.hooksPath .githooks
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_QUIET
        ERROR_QUIET
    )
endif()

include(FetchContent)

add_library(${PROJECT_NAME}_lib STATIC)
add_library(${PROJECT_NAME} SHARED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/reaper-plugin.cmake)
include(cmake/tests.cmake)
include(cmake/clang-tidy.cmake)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "main\\.cpp$")
target_sources(${PROJECT_NAME}_lib PRIVATE ${SOURCES})
target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src/main.cpp")

target_link_libraries(${PROJECT_NAME}_lib PRIVATE reaper-sdk)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib reaper-sdk)

if(MSVC)
    target_compile_options(
        ${PROJECT_NAME}_lib
        PUBLIC /W3 /WX /wd4996 /wd4244 /wd4267 /wd4305
    )
else()
    target_compile_options(
        ${PROJECT_NAME}_lib
        PUBLIC -Wall -Wextra -Werror -Wno-conversion -Wno-sign-conversion
    )
endif()

include(cmake/cpack.cmake)
