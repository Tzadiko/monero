# - try to find HIDAPI library
# from http://www.signal11.us/oss/hidapi/
#
# Cache Variables: (probably not for direct use in your scripts)
#  HIDAPI_INCLUDE_DIR
#  HIDAPI_LIBRARY
#
# Non-cache variables you might use in your CMakeLists.txt:
#  HIDAPI_FOUND
#  HIDAPI_INCLUDE_DIRS
#  HIDAPI_LIBRARIES
#
# Requires these CMake modules:
#  FindPackageHandleStandardArgs (known included with CMake >=2.6.2)
#
# Original Author:
# 2009-2010 Ryan Pavlik <rpavlik@iastate.edu> <abiryan@ryand.net>
# http://academic.cleardefinition.com
# Iowa State University HCI Graduate Program/VRAC
#
# Copyright Iowa State University 2009-2010.
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
# Monero addition: the libudev handling below, and the one configuration in
# which libudev is a mandatory build dependency, are described in
# docs/COMPILING_DEBUGGING_TESTING.md, section "Build-system changes beyond the
# dialect switch".

find_library(HIDAPI_LIBRARY
  NAMES hidapi hidapi-libusb)

find_path(HIDAPI_INCLUDE_DIR
  NAMES hidapi.h
  PATH_SUFFIXES
  hidapi)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HIDAPI
  DEFAULT_MSG
  HIDAPI_LIBRARY
  HIDAPI_INCLUDE_DIR)

if(HIDAPI_FOUND)
  set(HIDAPI_LIBRARIES "${HIDAPI_LIBRARY}")
  if((STATIC AND UNIX AND NOT APPLE) OR (DEPENDS AND CMAKE_SYSTEM_NAME STREQUAL "Linux") OR ANDROID)
    find_library(LIBUSB-1.0_LIBRARY usb-1.0)
    if(LIBUSB-1.0_LIBRARY)
      set(HIDAPI_LIBRARIES "${HIDAPI_LIBRARIES};${LIBUSB-1.0_LIBRARY}")

      # Hidapi is built without the udev backend in depends
      if (NOT DEPENDS)
        find_library(LIBUDEV_LIBRARY udev)
        if(LIBUDEV_LIBRARY)
          set(HIDAPI_LIBRARIES "${HIDAPI_LIBRARIES};${LIBUDEV_LIBRARY}")
        elseif(NOT ANDROID)
          # hidapi's own .pc declares no private dependencies, so ask pkg-config about
          # the libusb-1.0 this module has just appended to HIDAPI_LIBRARIES: if that
          # libusb names udev among its private static link dependencies, the archive
          # carries its udev backend and every executable linking hidapi/libusb ends the
          # build with "undefined reference to udev_device_get_action@@LIBUDEV_183". A
          # warning would only postpone that failure until the whole tree is compiled,
          # so fail at configure time. FreeBSD has no udev at all, and pkg-config may be
          # absent or report no udev (a libusb without the udev backend) - those cases
          # keep the historical warning.
          set(_HIDAPI_LIBUSB_STATIC_LIBRARIES "")
          find_package(PkgConfig QUIET)
          if(PKG_CONFIG_FOUND)
            pkg_check_modules(PKGCONFIG_HIDAPI_LIBUSB QUIET libusb-1.0)
            set(_HIDAPI_LIBUSB_STATIC_LIBRARIES "${PKGCONFIG_HIDAPI_LIBUSB_STATIC_LIBRARIES}")
          endif()
          if(NOT FREEBSD AND "${_HIDAPI_LIBUSB_STATIC_LIBRARIES}" MATCHES "(^|;)udev(;|$)")
            message(FATAL_ERROR "libudev library not found, but the libusb-1.0 linked alongside hidapi "
              "declares udev among its private dependencies (pkg-config libusb-1.0 static libraries: "
              "${_HIDAPI_LIBUSB_STATIC_LIBRARIES}). Linking would fail with undefined references to "
              "udev_device_* symbols. Install the udev development package - libudev-dev "
              "(Debian/Ubuntu), systemd-devel (Fedora), eudev-libudev-devel (Void) or systemd-libs "
              "(Arch) - or, if the local libusb was built without the udev backend, point the build at "
              "the library explicitly with -DLIBUDEV_LIBRARY=<path to libudev>.")
          else()
            message(WARNING "libudev library not found, binaries may fail to link.")
          endif()
        endif()
      endif()
    else()
      message(WARNING "libusb-1.0 library not found, binaries may fail to link.")
    endif()
    if(ANDROID)
      # libusb uses android log library
      find_library(ANDROID_LOG_LIBRARY log)
      if(ANDROID_LOG_LIBRARY)
        set(HIDAPI_LIBRARIES "${HIDAPI_LIBRARIES};${ANDROID_LOG_LIBRARY}")
      else()
        message(WARNING "Android log library not found, binaries may fail to link.")
      endif()
    endif()
  endif()

  set(HIDAPI_INCLUDE_DIRS "${HIDAPI_INCLUDE_DIR}")
endif()

mark_as_advanced(HIDAPI_INCLUDE_DIR HIDAPI_LIBRARY)
