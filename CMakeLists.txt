cmake_minimum_required(VERSION 4.0)

set(_version "0.0.0")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} tag --list --sort=-version:refname
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE _git_tags
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    string(REPLACE "\n" ";" _git_tags "${_git_tags}")
    foreach(_git_tag IN LISTS _git_tags)
        if(_git_tag MATCHES "^v?[0-9]+\\.[0-9]+\\.[0-9]+")
            string(
                REGEX MATCH "^v?[0-9]+\\.[0-9]+\\.[0-9]+"
                _version_prefix
                "${_git_tag}"
            )
            string(REGEX REPLACE "^v" "" _version "${_version_prefix}")
            break()
        endif()
    endforeach()
endif()

project(reallm VERSION ${_version} LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

add_library(${PROJECT_NAME}_lib STATIC)
add_library(${PROJECT_NAME} SHARED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/reaper-plugin.cmake)
include(cmake/tests.cmake)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "main\\.cpp$")
target_sources(${PROJECT_NAME}_lib PRIVATE ${SOURCES})
target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src/main.cpp")

target_link_libraries(${PROJECT_NAME}_lib PRIVATE reaper-sdk)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib reaper-sdk)

function(configure_target_clang_tidy target_name tidy_command)
    if(TARGET ${target_name})
        set_target_properties(
            ${target_name}
            PROPERTIES CXX_CLANG_TIDY "${tidy_command}"
        )
    endif()
endfunction()

set(_clang_tidy_enabled TRUE)
if(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES ";")
    set(_clang_tidy_enabled FALSE)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(_clang_tidy_enabled FALSE)
endif()

if(_clang_tidy_enabled)
    set(_clang_tidy_command "clang-tidy")
    if(WIN32)
        # MSVC release flags (for example /GL) are not understood by clang-tidy's
        # compile simulation and can be reported as unused command-line arguments.
        string(
            APPEND _clang_tidy_command
            ";--extra-arg=-Wno-unused-command-line-argument"
        )
    endif()

    configure_target_clang_tidy(${PROJECT_NAME}_lib "${_clang_tidy_command}")
    configure_target_clang_tidy(${PROJECT_NAME} "${_clang_tidy_command}")
    configure_target_clang_tidy(${PROJECT_NAME}_tests "${_clang_tidy_command}")
endif()
unset(_clang_tidy_enabled)

if(MSVC)
    target_compile_options(
        ${PROJECT_NAME}_lib
        PRIVATE /W3 /WX /wd4996 /wd4244 /wd4267 /wd4305
    )
else()
    target_compile_options(
        ${PROJECT_NAME}_lib
        PUBLIC -Wall -Wextra -Werror -Wno-conversion -Wno-sign-conversion
    )
endif()

include(cmake/cpack.cmake)
