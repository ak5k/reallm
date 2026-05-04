include_guard(GLOBAL)
include(FetchContent)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

FetchContent_MakeAvailable(WDL reaper_sdk)

FetchContent_GetProperties(WDL SOURCE_DIR _wdl_source_dir)
FetchContent_GetProperties(reaper_sdk SOURCE_DIR _reaper_sdk_source_dir)

if(NOT _wdl_source_dir OR NOT EXISTS "${_wdl_source_dir}/WDL")
    message(FATAL_ERROR "WDL source directory was not fetched correctly.")
endif()
if(NOT _reaper_sdk_source_dir OR NOT EXISTS "${_reaper_sdk_source_dir}/sdk")
    message(
        FATAL_ERROR
        "reaper-sdk source directory was not fetched correctly."
    )
endif()

# FindWDL.cmake resolves from this variable.
set(WDL_ROOT_DIR "${_wdl_source_dir}")
find_package(WDL REQUIRED)

# Ensure the REAPER SDK sees the WDL tree in the expected location.
execute_process(
    COMMAND
        ${CMAKE_COMMAND} -E create_symlink "${_wdl_source_dir}/WDL"
        "${_reaper_sdk_source_dir}/WDL"
)

if(NOT TARGET reaper-sdk)
    add_library(reaper-sdk IMPORTED INTERFACE)
    target_include_directories(
        reaper-sdk
        SYSTEM
        INTERFACE "${_reaper_sdk_source_dir}/sdk"
    )
endif()
target_link_libraries(reaper-sdk INTERFACE WDL::WDL)

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

if(NOT TARGET ${PROJECT_NAME})
    message(
        FATAL_ERROR
        "Include cmake/reaper-plugin.cmake after add_library(${PROJECT_NAME} ...)."
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
    RUNTIME
        DESTINATION
            "${REAPER_USER_PLUGINS}" # Windows .dll
    LIBRARY
        DESTINATION
            "${REAPER_USER_PLUGINS}" # Linux .so/macOS .dylib
)

if(WIN32)
    install(
        FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
        DESTINATION "${REAPER_USER_PLUGINS}"
        OPTIONAL
    )
endif()

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
endif()

if(WIN32)
    target_compile_definitions(${PROJECT_NAME}_lib PUBLIC NOMINMAX UNICODE)
endif()

if(NOT MSVC)
    target_compile_options(
        ${PROJECT_NAME}_lib
        PUBLIC -fsigned-char -fstack-protector-strong -fdiagnostics-color
    )
endif()
