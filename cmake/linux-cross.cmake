# heavily 'inspired' by cfillions reapack/reaimgui

if(NOT DEFINED ENV{ARCH})
  message(FATAL_ERROR "The ARCH environment variable is not set.")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR $ENV{ARCH})

if($ENV{ARCH} STREQUAL "i686")
  set(CMAKE_C_FLAGS -m32)
  set(CMAKE_CXX_FLAGS -m32)
elseif(NOT DEFINED ENV{TOOLCHAIN_PREFIX})
  message(FATAL_ERROR "The TOOLCHAIN_PREFIX environment variable is not set.")
else()
  set(CMAKE_C_COMPILER $ENV{TOOLCHAIN_PREFIX}-gcc)
  set(CMAKE_CXX_COMPILER $ENV{TOOLCHAIN_PREFIX}-g++)
endif()

if(DEFINED ENV{TOOLCHAIN_PREFIX})
  set(CMAKE_FIND_ROOT_PATH /usr/$ENV{TOOLCHAIN_PREFIX})
  set(CMAKE_LIBRARY_PATH /usr/lib/$ENV{TOOLCHAIN_PREFIX})
  set(CMAKE_INCLUDE_PATH /usr/include/$ENV{TOOLCHAIN_PREFIX})
  set(ENV{PKG_CONFIG_PATH} /usr/lib/$ENV{TOOLCHAIN_PREFIX}/pkgconfig)
endif()
