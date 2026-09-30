cmake_minimum_required(VERSION 3.16)

# The version lives in the public header, so builds that do not use CMake agree with this one.
file(STRINGS include/speechwarp.h speechwarp_version_line REGEX "#define SPEECHWARP_VERSION \"")
string(REGEX REPLACE ".*\"(.*)\".*" "\\1" speechwarp_version "${speechwarp_version_line}")

project(speechwarp VERSION ${speechwarp_version} LANGUAGES C
        DESCRIPTION "Nonlinear speed-up for speech")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(speechwarp_is_top_level ON)
else()
  set(speechwarp_is_top_level OFF)
endif()

option(SPEECHWARP_BUILD_SHARED "Build the shared library" ON)
option(SPEECHWARP_BUILD_STATIC "Build the static library" ON)
option(SPEECHWARP_FRAMEWORK "Build the shared library as an Apple framework" OFF)
option(SPEECHWARP_BUILD_TOOLS "Build the speechwarp command-line tool" ${speechwarp_is_top_level})
option(SPEECHWARP_BUILD_TESTS "Build the tests" ${speechwarp_is_top_level})
option(SPEECHWARP_INSTALL "Add install rules" ${speechwarp_is_top_level})

if(NOT SPEECHWARP_BUILD_SHARED AND NOT SPEECHWARP_BUILD_STATIC)
  message(FATAL_ERROR "Enable SPEECHWARP_BUILD_SHARED, SPEECHWARP_BUILD_STATIC or both")
endif()

# Release matters here: upstream is full of asserts, which NDEBUG removes.
if(speechwarp_is_top_level AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(speechwarp_third_party
  src/third_party_kissfft.c
  src/third_party_sonic.c
  src/third_party_speedy.c)
set(speechwarp_sources src/speechwarp.c src/fft.c ${speechwarp_third_party})

# Upstream code is compiled as it comes; its warnings are not ours to fix.
if(MSVC)
  set_source_files_properties(${speechwarp_third_party} PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(${speechwarp_third_party} PROPERTIES COMPILE_OPTIONS "-w")
endif()

include(CheckLibraryExists)
check_library_exists(m cos "" speechwarp_has_libm)

function(speechwarp_configure target)
  target_include_directories(${target}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE third_party/kissfft)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 99
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W3)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
  if(speechwarp_has_libm)
    target_link_libraries(${target} PRIVATE m)
  endif()
endfunction()

if(SPEECHWARP_BUILD_SHARED)
  add_library(speechwarp_shared SHARED ${speechwarp_sources})
  speechwarp_configure(speechwarp_shared)
  target_compile_definitions(speechwarp_shared PRIVATE SPEECHWARP_BUILD INTERFACE SPEECHWARP_SHARED)
  set_target_properties(speechwarp_shared PROPERTIES
    OUTPUT_NAME speechwarp
    EXPORT_NAME shared
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
  if(SPEECHWARP_FRAMEWORK)
    # What an iOS app embeds. The App Store wants the identifier, the versions and the minimum system.
    set_target_properties(speechwarp_shared PROPERTIES
      FRAMEWORK TRUE
      PUBLIC_HEADER include/speechwarp.h
      MACOSX_FRAMEWORK_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/framework-Info.plist.in
      MACOSX_FRAMEWORK_IDENTIFIER io.github.freddygaffey.speechwarp
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER io.github.freddygaffey.speechwarp
      MACOSX_FRAMEWORK_SHORT_VERSION_STRING ${PROJECT_VERSION}
      MACOSX_FRAMEWORK_BUNDLE_VERSION ${PROJECT_VERSION})
  endif()
  add_library(speechwarp::shared ALIAS speechwarp_shared)
  list(APPEND speechwarp_targets speechwarp_shared)
endif()

if(SPEECHWARP_BUILD_STATIC)
  add_library(speechwarp_static STATIC ${speechwarp_sources})
  speechwarp_configure(speechwarp_static)
  set_target_properties(speechwarp_static PROPERTIES EXPORT_NAME static)
  if(NOT WIN32)
    # On Windows the name is taken by the shared library's import library.
    set_target_properties(speechwarp_static PROPERTIES OUTPUT_NAME speechwarp)
  endif()
  add_library(speechwarp::static ALIAS speechwarp_static)
  list(APPEND speechwarp_targets speechwarp_static)
endif()

if(SPEECHWARP_BUILD_TOOLS)
  add_executable(speechwarp_tool tools/speechwarp.c)
  set_target_properties(speechwarp_tool PROPERTIES OUTPUT_NAME speechwarp C_STANDARD 99)
  if(SPEECHWARP_BUILD_STATIC)
    target_link_libraries(speechwarp_tool PRIVATE speechwarp_static)
  else()
    target_link_libraries(speechwarp_tool PRIVATE speechwarp_shared)
  endif()
  if(MSVC)
    target_compile_options(speechwarp_tool PRIVATE /W3)
    target_compile_definitions(speechwarp_tool PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(speechwarp_tool PRIVATE -Wall -Wextra)
  endif()
  list(APPEND speechwarp_programs speechwarp_tool)
endif()

if(SPEECHWARP_BUILD_TESTS)
  if(NOT SPEECHWARP_BUILD_STATIC)
    message(FATAL_ERROR "The tests need SPEECHWARP_BUILD_STATIC")
  endif()
  enable_testing()
  add_subdirectory(tests)
endif()

if(SPEECHWARP_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)
  install(TARGETS ${speechwarp_targets} EXPORT speechwarp-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FRAMEWORK DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  if(speechwarp_programs)
    install(TARGETS ${speechwarp_programs} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
  if(NOT SPEECHWARP_FRAMEWORK)
    install(FILES include/speechwarp.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  endif()
  install(EXPORT speechwarp-targets NAMESPACE speechwarp::
    FILE speechwarp-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/speechwarp)
  write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/speechwarp-config-version.cmake
    COMPATIBILITY SameMajorVersion)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/speechwarp-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/speechwarp)
  install(FILES LICENSE NOTICE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/speechwarp)
  install(DIRECTORY third_party/ DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/speechwarp/third_party
    FILES_MATCHING PATTERN "LICENSE" PATTERN "COPYING" PATTERN "LICENSES/*" PATTERN "README.md")
endif()
