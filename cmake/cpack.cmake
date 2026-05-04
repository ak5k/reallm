include_guard(GLOBAL)

# ── Project-specific data ────────────────────────────────────────────────────
# Adapt these variables when reusing this file in another project.

set(_pkg_vendor "ak5k")
set(_pkg_homepage "https://github.com/ak5k/reallm")
set(_pkg_contact "https://github.com/ak5k/reallm/issues")
set(_pkg_description_summary
    "Cubase/Logic style low latency monitoring mode for REAPER"
)
set(_pkg_description_long
    "Cubase/Logic style low latency monitoring mode for REAPER.\
 While enabled, restricts PDC latency to one block/buffer size by\
 bypassing plugins on input-monitored signal chains."
)
set(_pkg_deb_maintainer "ak5k <https://github.com/ak5k>")
set(_pkg_deb_section "sound")
# NSIS: parent directory under CPACK_NSIS_INSTALL_ROOT that holds the payload.
set(_pkg_nsis_install_root "$APPDATA")
set(_pkg_nsis_install_directory "REAPER")
# productbuild: relative install prefix (under the user's home directory).
set(_pkg_productbuild_install_prefix "Library/Application Support/REAPER")
set(_pkg_productbuild_identifier "io.github.ak5k.reallm")
# TODO/FIXME: Set a Start Menu shortcut target (REAPER exe, folder, URL, …).
# When set, NSIS will create and delete the shortcut automatically.
# Leave empty to add no shortcut.
set(_pkg_nsis_shortcut_target "")
set(_pkg_nsis_shortcut_name "")

# ── Common metadata ──────────────────────────────────────────────────────────

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "${_pkg_vendor}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${_pkg_description_summary}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${_pkg_homepage}")

# License and readme resources shown in installer UI.
configure_file(
    "${CMAKE_SOURCE_DIR}/LICENSE"
    "${CMAKE_BINARY_DIR}/LICENSE.txt"
    COPYONLY
)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_BINARY_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")

# Package file name: <name>-<version>-<platform>-<arch>
if(WIN32)
    if(MSVC_CXX_ARCHITECTURE_ID)
        string(TOLOWER "${MSVC_CXX_ARCHITECTURE_ID}" _pkg_arch)
    else()
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _pkg_arch)
    endif()
    set(CPACK_SYSTEM_NAME "windows-${_pkg_arch}")
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES)
        list(JOIN CMAKE_OSX_ARCHITECTURES "-" _pkg_arch)
        string(TOLOWER "${_pkg_arch}" _pkg_arch)
    else()
        string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _pkg_arch)
    endif()
    set(CPACK_SYSTEM_NAME "macos-${_pkg_arch}")
else()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _pkg_arch)
    set(CPACK_SYSTEM_NAME "linux-${_pkg_arch}")
endif()

set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}"
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_STRIP_FILES OFF)

# Generator selection.
if(WIN32)
    set(CPACK_GENERATOR "NSIS")
elseif(APPLE)
    set(CPACK_GENERATOR "productbuild")
else()
    set(CPACK_GENERATOR "DEB")
endif()

# ── NSIS (Windows) ──────────────────────────────────────────────────────────
if(WIN32)
    set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME}")
    set(CPACK_NSIS_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_NSIS_INSTALL_ROOT "${_pkg_nsis_install_root}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${_pkg_nsis_install_directory}")
    set(CPACK_NSIS_COMPRESSOR "zlib")
    set(CPACK_NSIS_CONTACT "${_pkg_contact}")
    set(CPACK_NSIS_URL_INFO_ABOUT "${_pkg_homepage}")
    set(CPACK_NSIS_MENU_LINKS "")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

    if(_pkg_nsis_shortcut_target)
        set(CPACK_NSIS_CREATE_ICONS_EXTRA
            "CreateShortcut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${_pkg_nsis_shortcut_name}.lnk' '${_pkg_nsis_shortcut_target}'"
        )
        set(CPACK_NSIS_DELETE_ICONS_EXTRA
            "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${_pkg_nsis_shortcut_name}.lnk'"
        )
    else()
        set(CPACK_NSIS_CREATE_ICONS_EXTRA "")
        set(CPACK_NSIS_DELETE_ICONS_EXTRA "")
    endif()
endif()

# ── productbuild (macOS) ─────────────────────────────────────────────────────
if(APPLE)
    set(CPACK_PRODUCTBUILD_IDENTIFIER "${_pkg_productbuild_identifier}")
    set(CPACK_INSTALL_PREFIX "${_pkg_productbuild_install_prefix}")
    # Install into the current user's domain only (no admin/root prompt).
    set(CPACK_PRODUCTBUILD_DOMAINS ON)
    set(CPACK_PRODUCTBUILD_DOMAINS_ANYWHERE OFF)
    set(CPACK_PRODUCTBUILD_DOMAINS_ROOT OFF)
    set(CPACK_PRODUCTBUILD_DOMAINS_USER ON)
endif()

# ── DEB (Linux) ──────────────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${_pkg_deb_maintainer}")
    set(CPACK_DEBIAN_PACKAGE_SECTION "${_pkg_deb_section}")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${_pkg_homepage}")
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${_pkg_description_long}")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    # TODO/FIXME: Override CPACK_PACKAGING_INSTALL_PREFIX for system-wide installs,
    # e.g.: cmake -DCPACK_PACKAGING_INSTALL_PREFIX=/usr/lib/REAPER ...
    # Defaults to CMAKE_INSTALL_PREFIX if not set.
endif()

include(CPack)
