function(reaper_plugin_fetch_dependencies)
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

    FetchContent_MakeAvailable(WDL reaper_sdk)

    # FindWDL.cmake resolves from this variable.
    set(WDL_ROOT_DIR "${wdl_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

function(reaper_plugin_setup_sdk)
    # Ensure the REAPER SDK sees the WDL tree in the expected location.
    execute_process(
        COMMAND
            ${CMAKE_COMMAND} -E create_symlink "${wdl_SOURCE_DIR}/WDL"
            "${reaper_sdk_SOURCE_DIR}/WDL"
    )

    if(NOT TARGET reaper-sdk)
        add_library(reaper-sdk IMPORTED INTERFACE)
        target_include_directories(
            reaper-sdk
            SYSTEM
            INTERFACE "${reaper_sdk_SOURCE_DIR}/sdk"
        )
    endif()

    target_link_libraries(reaper-sdk INTERFACE WDL::WDL)
endfunction()

function(reaper_plugin_configure_install_prefix)
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
endfunction()

function(reaper_plugin_configure_target plugin_target)
    if(CMAKE_OSX_ARCHITECTURES)
        list(JOIN CMAKE_OSX_ARCHITECTURES "-" ARCH_NAME)
    elseif(MSVC_CXX_ARCHITECTURE_ID)
        set(ARCH_NAME ${MSVC_CXX_ARCHITECTURE_ID})
    else()
        set(ARCH_NAME ${CMAKE_SYSTEM_PROCESSOR})
    endif()

    string(TOLOWER "${ARCH_NAME}" ARCH_NAME)
    set_target_properties(
        ${plugin_target}
        PROPERTIES
            PREFIX
                "" # disable the "lib" prefix
            OUTPUT_NAME "reaper_${plugin_target}-${ARCH_NAME}"
    )
endfunction()

function(reaper_plugin_install_target plugin_target)
    set(REAPER_USER_PLUGINS "UserPlugins")
    install(
        TARGETS ${plugin_target}
        RUNTIME
            DESTINATION
                "${REAPER_USER_PLUGINS}" # Windows .dll
        LIBRARY
            DESTINATION
                "${REAPER_USER_PLUGINS}" # Linux .so/macOS .dylib
    )

    if(WIN32)
        install(
            FILES $<TARGET_PDB_FILE:${plugin_target}>
            DESTINATION "${REAPER_USER_PLUGINS}"
        )
    endif()
endfunction()
