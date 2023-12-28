cmake_minimum_required(VERSION 3.15)

project(reallm VERSION 0.5.0 LANGUAGES C CXX)
add_library(${PROJECT_NAME} SHARED)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)

include(CheckCompilerFlag)
include(CheckIncludeFileCXX)
include(CheckLinkerFlag)
include(FindGit)
include(FindThreads)

if(NOT EXISTS ${PROJECT_SOURCE_DIR}/vendor/reaper-sdk/README)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive --depth 1 
      WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
      )
endif()

if(WIN32)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# path for external 3rd party library dependencies 
set(PROJECT_LIB_DIR ${PROJECT_SOURCE_DIR}/vendor)

FILE(GLOB header_paths 
    ${PROJECT_LIB_DIR}/*/include
    ${PROJECT_LIB_DIR}/*/sdk
    )

target_include_directories(${PROJECT_NAME} SYSTEM PRIVATE ${header_paths})


# symlink WDL to reaper-sdk
execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            # source
            "${PROJECT_LIB_DIR}/WDL/WDL"
            # target
            "${PROJECT_LIB_DIR}/reaper-sdk/WDL"
)

if(WIN32)
  target_compile_options(
    ${PROJECT_NAME} 
      PRIVATE 
      /W3 /WX
      /wd4244
      /wd4996
      )
else()
  target_compile_options(
    ${PROJECT_NAME}
      PRIVATE
      -Wall -Wextra -Werror -Wpedantic 
      -Wno-conversion
      -Wno-format
      -Wno-implicit-fallthrough 
      -Wno-missing-braces
      -Wno-missing-field-initializers
      -Wno-shadow
      -Wno-sign-compare
      -Wno-switch 
      -Wno-uninitialized 
      -Wno-unused 
      -Wno-unused-function 
      -Wno-unused-label
      -Wno-unused-parameter
      -Wno-unused-value
      -Wno-unused-variable
  )
endif()

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "")
endif()

if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT DEFINED VCPKG_TARGET_TRIPLET)
  set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(WDL REQUIRED)

if(NOT WIN32)
  find_package(SWELL REQUIRED)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE WDL::WDL)

if(SWELL_FOUND)
  target_link_libraries(${PROJECT_NAME} PRIVATE SWELL::swell)

  # Transpile the Win32 resource file
  find_package(PHP REQUIRED)
  add_custom_command(
    OUTPUT  resource.rc_mac_dlg resource.rc_mac_menu
    COMMAND ${CMAKE_COMMAND} -E copy
      ${CMAKE_CURRENT_SOURCE_DIR}/resource.rc ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${PHP_EXECUTABLE} ${SWELL_RESGEN} --quiet resource.rc
    DEPENDS ${SWELL_RESGEN}
    MAIN_DEPENDENCY resource.rc
  )
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(WIN32)
    set(USER_CONFIG_DIR "$ENV{APPDATA}")
  elseif(APPLE)
    set(USER_CONFIG_DIR "~/Library/Application Support")
  else()
    set(USER_CONFIG_DIR "~/.config")
  endif()

  set(CMAKE_INSTALL_PREFIX "${USER_CONFIG_DIR}/REAPER" CACHE PATH
    "REAPER resource path where to install ReaPack" FORCE)
endif()

if(CMAKE_OSX_ARCHITECTURES)
  list(JOIN CMAKE_OSX_ARCHITECTURES "-" ARCH_NAME)
elseif(MSVC_CXX_ARCHITECTURE_ID)
  set(ARCH_NAME ${MSVC_CXX_ARCHITECTURE_ID})
else()
  set(ARCH_NAME ${CMAKE_SYSTEM_PROCESSOR})
endif()

STRING(TOLOWER "${ARCH_NAME}" ARCH_NAME)

if(VCPKG_TOOLCHAIN)
  set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Release)
  set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX "" # disable the "lib" prefix
  OUTPUT_NAME "reaper_${PROJECT_NAME}-${ARCH_NAME}"
)

# if(WIN32)
#   add_link_options(
#     # Use relative path to the PDB file to avoid exposing the full path
#     /PDBALTPATH:%_PDB%
#   )
# endif()

add_subdirectory(source)

set(REAPER_USER_PLUGINS "UserPlugins")
install(TARGETS ${PROJECT_NAME}
  COMPONENT ${PROJECT_NAME}
  RUNTIME DESTINATION "${REAPER_USER_PLUGINS}" # Windows .dll
  LIBRARY DESTINATION "${REAPER_USER_PLUGINS}" # Linux .so/macOS .dylib
)