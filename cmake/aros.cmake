# Cross toolchain file for AROS, used for the CMake-built parts of the kit and
# usable by programs that consume it. Values come from the cache variables
# AROS_CC, AROS_CXX, AROS_AR, AROS_SDK and AROS_CFLAGS (set by build.py).
set(CMAKE_SYSTEM_NAME AROS)
set(CMAKE_SYSTEM_VERSION 1)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES AROS_CC AROS_CXX AROS_AR AROS_SDK AROS_CFLAGS)
set(CMAKE_C_COMPILER "${AROS_CC}")
if(AROS_CXX)
  set(CMAKE_CXX_COMPILER "${AROS_CXX}")
endif()
set(CMAKE_AR "${AROS_AR}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_FLAGS_INIT "${AROS_CFLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${AROS_CFLAGS}")
set(CMAKE_SYSROOT "${AROS_SDK}")
set(CMAKE_FIND_ROOT_PATH "${AROS_SDK}")
# CMake has no AROS platform file: the SDK keeps include/ and lib/ at its top.
set(CMAKE_SYSTEM_PREFIX_PATH "/")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# Only the SDK (and paths passed explicitly, such as <Package>_DIR): nothing
# from the build machine, e.g. Homebrew packages, may end up in an AROS program.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(ENV{PKG_CONFIG_LIBDIR} "${AROS_SDK}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${AROS_SDK}")
# Configure checks link real executables: the AROS linker fails on a missing
# symbol or library, so function and library probes give true answers.
