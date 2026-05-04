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
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

FetchContent_Declare(
    WDL
    GIT_REPOSITORY https://github.com/justinfrankel/WDL
    GIT_TAG "origin/main"
    SYSTEM
)

FetchContent_Declare(
    reaper_sdk
    GIT_REPOSITORY https://github.com/justinfrankel/reaper-sdk
    GIT_TAG "origin/main"
    SYSTEM
)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest
    GIT_TAG v1.17.0
    SYSTEM
)

FetchContent_MakeAvailable(WDL reaper_sdk googletest)

# symlink WDL to reaper-sdk
execute_process(
    COMMAND
        ${CMAKE_COMMAND} -E create_symlink "${wdl_SOURCE_DIR}/WDL"
        "${reaper_sdk_SOURCE_DIR}/WDL"
)

set(WDL_ROOT_DIR "${wdl_SOURCE_DIR}")

add_library(reaper-sdk IMPORTED INTERFACE)
target_include_directories(
    reaper-sdk
    SYSTEM
    INTERFACE "${reaper_sdk_SOURCE_DIR}/sdk"
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(WDL REQUIRED)

target_link_libraries(reaper-sdk INTERFACE WDL::WDL)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)

add_library(${PROJECT_NAME}_lib STATIC)
add_library(${PROJECT_NAME} SHARED)
enable_testing()
include(GoogleTest)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "_test\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX "main\\.cpp$")
target_sources(${PROJECT_NAME}_lib PRIVATE ${SOURCES})
target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src/main.cpp")

target_link_libraries(${PROJECT_NAME}_lib PRIVATE reaper-sdk)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib reaper-sdk)

file(
    GLOB_RECURSE _test_sources
    CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*_test.cpp"
)
if(_test_sources)
    add_executable(${PROJECT_NAME}_tests ${_test_sources})
    target_link_libraries(
        ${PROJECT_NAME}_tests
        PRIVATE ${PROJECT_NAME}_lib GTest::gtest_main
    )
    gtest_discover_tests(${PROJECT_NAME}_tests DISCOVERY_MODE PRE_TEST)
endif()

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

target_compile_definitions(
    ${PROJECT_NAME}_lib
    PRIVATE
        REALLM_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        REALLM_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        REALLM_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(MSVC)
    foreach(
        arg
        CMAKE_C_FLAGS_DEBUG
        CMAKE_CXX_FLAGS_DEBUG
        CMAKE_C_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_RELEASE
        CMAKE_C_FLAGS_RELWITHDEBINFO
        CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS_MINSIZEREL
        CMAKE_CXX_FLAGS_MINSIZEREL
    )
        # Embed debug information in the object files
        string(REGEX REPLACE "/Z[iI]" "/Z7" ${arg} "${${arg}}")
    endforeach()

    add_compile_options(
        # Exception handling model
        /EHsc
        # Enhance optimized debugging
        $<$<NOT:$<CONFIG:Debug>>:/Zo>
        # Eliminate duplicate strings
        $<$<NOT:$<CONFIG:Debug>>:/GF>
        # Enable function-level linking
        $<$<NOT:$<CONFIG:Debug>>:/Gy>
        # Remove unreferenced COMDAT
        $<$<NOT:$<CONFIG:Debug>>:/Zc:inline>
    )

    target_compile_options(
        ${PROJECT_NAME}_lib
        PRIVATE /W3 /WX /wd4996 /wd4244 /wd4267 /wd4305
    )

    if(NOT CMAKE_PROJECT_VERSION_TWEAK EQUAL "1")
        add_link_options(
            # Remove unreferenced code
            $<$<NOT:$<CONFIG:Debug>>:/OPT:REF>
            # Remove duplicate sections
            $<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>
            # Use relative path to the PDB file to avoid exposing the full path
            /PDBALTPATH:%_PDB%
        )
    endif()
else()
    target_compile_options(
        ${PROJECT_NAME}_lib
        PUBLIC
            -fsigned-char
            -fstack-protector-strong
            -fdiagnostics-color
            -Wall
            -Wextra
            -Werror
            -Wno-conversion
            -Wno-sign-conversion
    )
endif()

if(WIN32)
    target_compile_definitions(${PROJECT_NAME}_lib PUBLIC NOMINMAX UNICODE)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(WIN32)
        set(USER_CONFIG_DIR "$ENV{APPDATA}")
    elseif(APPLE)
        set(USER_CONFIG_DIR "~/Library/Application Support")
    else()
        set(USER_CONFIG_DIR "~/.config")
    endif()

    set(CMAKE_INSTALL_PREFIX
        "${USER_CONFIG_DIR}/REAPER"
        CACHE PATH
        "REAPER resource path where to install ReaPack"
        FORCE
    )
endif()

if(CMAKE_OSX_ARCHITECTURES)
    list(JOIN CMAKE_OSX_ARCHITECTURES "-" ARCH_NAME)
elseif(MSVC_CXX_ARCHITECTURE_ID)
    set(ARCH_NAME ${MSVC_CXX_ARCHITECTURE_ID})
else()
    set(ARCH_NAME ${CMAKE_SYSTEM_PROCESSOR})
endif()

string(TOLOWER "${ARCH_NAME}" ARCH_NAME)
set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES
        PREFIX
            "" # disable the "lib" prefix
        OUTPUT_NAME "reaper_${PROJECT_NAME}-${ARCH_NAME}"
)

set(REAPER_USER_PLUGINS "UserPlugins")
install(
    TARGETS ${PROJECT_NAME}
    COMPONENT ${PROJECT_NAME}
    RUNTIME
        DESTINATION
            "${REAPER_USER_PLUGINS}" # Windows .dll
    LIBRARY
        DESTINATION
            "${REAPER_USER_PLUGINS}" # Linux .so/macOS .dylib
)

# Set the component as required
set(CPACK_COMPONENT_${PROJECT_NAME}_REQUIRED ON)
if(WIN32)
    install(
        FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
        COMPONENT ${PROJECT_NAME}
        DESTINATION "${REAPER_USER_PLUGINS}"
    )
endif()

if(APPLE)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/Library/Application Support/REAPER")
    set(CPACK_COMPONENTS_ALL ${PROJECT_NAME})
    set(CPACK_GENERATOR productbuild)
elseif(WIN32)
    set(CPACK_GENERATOR NSIS)
else()
    set(CPACK_GENERATOR TXZ)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL "8")
    set(CPUBITS "64")
else()
    set(CPUBITS "32")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-macOS")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    execute_process(
        COMMAND
            ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "${CMAKE_CURRENT_BINARY_DIR}/link_readme.txt"
    )
    set(CPACK_RESOURCE_FILE_README
        "${CMAKE_CURRENT_BINARY_DIR}/link_readme.txt"
    )
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    execute_process(
        COMMAND
            ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CMAKE_CURRENT_BINARY_DIR}/link_license.txt"
    )
    set(CPACK_RESOURCE_FILE_LICENSE
        "${CMAKE_CURRENT_BINARY_DIR}/link_license.txt"
    )
endif()

set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_NSIS_CPUBITS ${CPUBITS})

include(CPack)
