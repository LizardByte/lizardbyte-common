#
# Project configuration
#
cmake_minimum_required(VERSION 3.24)
project(
  lizardbyte_common
  VERSION 0.0.0
  DESCRIPTION "Common helpers and tooling for LizardByte projects."
  HOMEPAGE_URL "https://app.lizardbyte.dev"
  LANGUAGES CXX)

set(PROJECT_LICENSE "MIT")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LIZARDBYTE_COMMON_IS_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(LIZARDBYTE_COMMON_IS_TOP_LEVEL ON)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting build type to 'Release' as none was specified.")
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "Choose the type of build." FORCE)
endif()

#
# Project optional configuration
#
option(BUILD_TESTS "Build tests" ${LIZARDBYTE_COMMON_IS_TOP_LEVEL})
option(LIZARDBYTE_COMMON_WARNINGS_AS_ERRORS
       "Treat lizardbyte-common warnings as errors"
       ${LIZARDBYTE_COMMON_IS_TOP_LEVEL})

set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

#
# Additional setup for coverage
# https://gcovr.com/en/stable/guide/compiling.html#compiler-options
#
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME
   AND BUILD_TESTS
   AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(CMAKE_CXX_FLAGS "-fprofile-arcs -ftest-coverage -ggdb -O0")
  set(CMAKE_C_FLAGS "-fprofile-arcs -ftest-coverage -ggdb -O0")
endif()

#
# Library code is located here When building tests this must be after the
# coverage flags are set
#
add_subdirectory(src)

#
# Tests are top-level only
#
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

#
# Package config
#
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/lizardbyte-common-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/lizardbyte-common-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/lizardbyte-common")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/lizardbyte-common-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/lizardbyte-common-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/lizardbyte-common-config-version.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/lizardbyte-common")
