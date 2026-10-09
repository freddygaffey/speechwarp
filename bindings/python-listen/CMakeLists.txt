cmake_minimum_required(VERSION 3.16)

# Builds the speechwarp_listen shared library for the Python package, from the repository (a clone with the
# whisper.cpp submodule) or, in a source distribution, from vendor/ (see vendor.py).
project(speechwarp_listen_python LANGUAGES C CXX)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vendor/listen/CMakeLists.txt)
  set(speechwarp_root ${CMAKE_CURRENT_SOURCE_DIR}/vendor)
else()
  get_filename_component(speechwarp_root ${CMAKE_CURRENT_SOURCE_DIR}/../.. ABSOLUTE)
endif()

foreach(off SPEECHWARP_LISTEN_BUILD_STATIC SPEECHWARP_LISTEN_BUILD_TESTS SPEECHWARP_LISTEN_BUILD_TOOLS
            SPEECHWARP_LISTEN_INSTALL)
  set(${off} OFF CACHE BOOL "Set by the Python package" FORCE)
endforeach()
# Built for any machine of its kind, not tuned to the one that builds it: x86-64 needs AVX2, FMA and F16C (2013 on).
set(GGML_NATIVE OFF CACHE BOOL "Set by the Python package" FORCE)
if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64"
   OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"))
  foreach(on GGML_SSE42 GGML_AVX GGML_AVX2 GGML_FMA GGML_F16C GGML_BMI2)
    set(${on} ON CACHE BOOL "Set by the Python package" FORCE)
  endforeach()
endif()
if(APPLE)
  # whisper.cpp's use of Accelerate's BLAS needs macOS 13.3, and so does its GPU code here. The published wheels are
  # built with MACOSX_DEPLOYMENT_TARGET=13.3 and have both; a build for older systems goes without.
  if(CMAKE_OSX_DEPLOYMENT_TARGET AND CMAKE_OSX_DEPLOYMENT_TARGET VERSION_GREATER_EQUAL 13.3)
    set(SPEECHWARP_LISTEN_METAL ON CACHE BOOL "Set by the Python package" FORCE)
  else()
    message(STATUS "MACOSX_DEPLOYMENT_TARGET is not 13.3 or later: building without Accelerate's BLAS and Metal")
    set(GGML_BLAS OFF CACHE BOOL "Set by the Python package" FORCE)
    set(SPEECHWARP_LISTEN_METAL OFF CACHE BOOL "Set by the Python package" FORCE)
  endif()
endif()

add_subdirectory(${speechwarp_root}/listen listen)

# One plain file in the package: wheels cannot hold the versioned names and their links.
set_property(TARGET speechwarp_listen_shared PROPERTY VERSION)
set_property(TARGET speechwarp_listen_shared PROPERTY SOVERSION)
install(TARGETS speechwarp_listen_shared
  LIBRARY DESTINATION speechwarp_listen
  RUNTIME DESTINATION speechwarp_listen
  ARCHIVE EXCLUDE_FROM_ALL)
install(FILES ${speechwarp_root}/LICENSE ${speechwarp_root}/NOTICE DESTINATION ${SKBUILD_METADATA_DIR}/licenses)
install(FILES ${speechwarp_root}/third_party/whisper.cpp/LICENSE
        DESTINATION ${SKBUILD_METADATA_DIR}/licenses/whisper.cpp)
