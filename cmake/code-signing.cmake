include_guard(GLOBAL)

if(NOT APPLE)
    return()
endif()

option(CODESIGN_ALL_BINARIES "Codesign project binaries on macOS." ON)
option(
    CODESIGN_REQUIRE_NOTARIZATION
    "Fail configuration unless code signing is configured with a Developer ID identity suitable for notarization."
    OFF
)
set(CODESIGN_IDENTITY
    "$ENV{CODESIGN_IDENTITY}"
    CACHE STRING
    "macOS codesign identity. Use '-' for ad-hoc signing."
)
set(CODESIGN_INSTALLER_IDENTITY
    "$ENV{CODESIGN_INSTALLER_IDENTITY}"
    CACHE STRING
    "macOS Developer ID Installer identity for signing .pkg files (productbuild)."
)
set(CODESIGN_ENTITLEMENTS
    ""
    CACHE FILEPATH
    "Optional entitlements plist passed to codesign."
)

if(NOT CODESIGN_ALL_BINARIES)
    return()
endif()

find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
find_program(_security_executable security REQUIRED)

if(CODESIGN_IDENTITY STREQUAL "")
    execute_process(
        COMMAND "${_security_executable}" find-identity -v -p codesigning
        OUTPUT_VARIABLE _keychain_identities
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_keychain_identities MATCHES "\"(Developer ID Application: [^\"]+)\"")
        set(CODESIGN_IDENTITY
            "${CMAKE_MATCH_1}"
            CACHE STRING
            "macOS codesign identity. Use '-' for ad-hoc signing."
            FORCE
        )
    else()
        set(CODESIGN_IDENTITY
            "-"
            CACHE STRING
            "macOS codesign identity. Use '-' for ad-hoc signing."
            FORCE
        )
    endif()
endif()

if(CODESIGN_INSTALLER_IDENTITY STREQUAL "")
    execute_process(
        COMMAND "${_security_executable}" find-identity -v -p basic
        OUTPUT_VARIABLE _keychain_identities
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_keychain_identities MATCHES "\"(Developer ID Installer: [^\"]+)\"")
        set(CODESIGN_INSTALLER_IDENTITY
            "${CMAKE_MATCH_1}"
            CACHE STRING
            "macOS Developer ID Installer identity for signing .pkg files (productbuild)."
            FORCE
        )
    endif()
endif()

if(CODESIGN_IDENTITY STREQUAL "-")
    if(CODESIGN_REQUIRE_NOTARIZATION)
        message(
            FATAL_ERROR
            "Apple notarization requires a Developer ID Application signing identity. Set CODESIGN_IDENTITY accordingly."
        )
    endif()

    message(
        STATUS
        "Using ad-hoc code signing. This is not notarization-ready; set CODESIGN_IDENTITY to a Developer ID Application certificate for notarization."
    )
endif()

function(codesign_target target)
    if(NOT TARGET ${target})
        return()
    endif()

    get_target_property(_is_imported ${target} IMPORTED)
    if(_is_imported)
        return()
    endif()

    get_target_property(_target_type ${target} TYPE)
    if(
        NOT _target_type STREQUAL "EXECUTABLE"
        AND NOT _target_type STREQUAL "SHARED_LIBRARY"
        AND NOT _target_type STREQUAL "MODULE_LIBRARY"
    )
        return()
    endif()

    set(_codesign_args --force --sign "${CODESIGN_IDENTITY}")
    if(NOT CODESIGN_IDENTITY STREQUAL "-")
        list(APPEND _codesign_args --timestamp)
    endif()
    if(
        _target_type STREQUAL "EXECUTABLE"
        AND NOT CODESIGN_IDENTITY STREQUAL "-"
    )
        list(APPEND _codesign_args --options runtime)
    endif()
    if(_target_type STREQUAL "EXECUTABLE" AND CODESIGN_ENTITLEMENTS)
        list(APPEND _codesign_args --entitlements "${CODESIGN_ENTITLEMENTS}")
    endif()

    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND
            "${CODESIGN_EXECUTABLE}" ${_codesign_args}
            "$<TARGET_FILE:${target}>"
        VERBATIM
        COMMENT "Codesigning $<TARGET_FILE_NAME:${target}>"
    )
endfunction()

get_property(_codesign_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_codesign_target IN LISTS _codesign_targets)
    codesign_target(${_codesign_target})
endforeach()
