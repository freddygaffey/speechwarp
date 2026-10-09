cmake_minimum_required(VERSION 3.16)

# speechwarp_listen: speech to text with whisper.cpp, as a module separate from the speechwarp library.
# Configure this directory on its own:
#     cmake -S listen -B build-listen -G Ninja && cmake --build build-listen && ctest --test-dir build-listen
# It needs whisper.cpp, a git submodule that is not fetched by default:
#     git submodule update --init --checkout third_party/whisper.cpp

# The version is speechwarp's, from its public header.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../include/speechwarp.h speechwarp_version_line
     REGEX "#define SPEECHWARP_VERSION \"")
string(REGEX REPLACE ".*\"(.*)\".*" "\\1" speechwarp_version "${speechwarp_version_line}")

project(speechwarp_listen VERSION ${speechwarp_version} LANGUAGES C CXX
        DESCRIPTION "Speech to text with whisper.cpp, for speechwarp")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(listen_is_top_level ON)
else()
  set(listen_is_top_level OFF)
endif()

option(SPEECHWARP_LISTEN_BUILD_SHARED "Build the shared library" ON)
option(SPEECHWARP_LISTEN_BUILD_STATIC "Build the static library" ON)
option(SPEECHWARP_LISTEN_METAL "Use the GPU through Metal on Apple platforms" OFF)
option(SPEECHWARP_LISTEN_BUILD_TOOLS "Build speechwarp-transcribe" ${listen_is_top_level})
option(SPEECHWARP_LISTEN_BUILD_TESTS "Build the tests" ${listen_is_top_level})
option(SPEECHWARP_LISTEN_INSTALL "Add install rules" ${listen_is_top_level})

if(NOT SPEECHWARP_LISTEN_BUILD_SHARED AND NOT SPEECHWARP_LISTEN_BUILD_STATIC)
  message(FATAL_ERROR "Enable SPEECHWARP_LISTEN_BUILD_SHARED, SPEECHWARP_LISTEN_BUILD_STATIC or both")
endif()

if(listen_is_top_level AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(listen_root ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(whisper_dir ${listen_root}/third_party/whisper.cpp)
if(NOT EXISTS ${whisper_dir}/CMakeLists.txt)
  message(FATAL_ERROR "whisper.cpp is missing. Fetch it with:\n"
          "  git submodule update --init --checkout third_party/whisper.cpp")
endif()

# ---- whisper.cpp and ggml: static, position independent, every symbol hidden, CPU only unless asked.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Set by speechwarp_listen: whisper.cpp and ggml are linked in" FORCE)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
if(NOT MSVC)
  # ggml has tentative definitions; as common symbols they would stay global in the static library.
  string(APPEND CMAKE_C_FLAGS " -fno-common")
endif()
foreach(off WHISPER_BUILD_TESTS WHISPER_BUILD_EXAMPLES WHISPER_BUILD_SERVER WHISPER_ALL_WARNINGS
            WHISPER_CURL WHISPER_SDL2 GGML_OPENMP GGML_BACKEND_DL)
  set(${off} OFF CACHE BOOL "Set by speechwarp_listen" FORCE)
endforeach()
# OpenMP is off so that the library needs no OpenMP runtime; ggml's own thread pool is used instead.
set(GGML_METAL ${SPEECHWARP_LISTEN_METAL} CACHE BOOL "Set by speechwarp_listen" FORCE)
if(SPEECHWARP_LISTEN_METAL)
  # The shaders go inside the library, so there is no .metallib file to ship.
  set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "Set by speechwarp_listen" FORCE)
endif()
set(WHISPER_BUILD_IS_DEV OFF CACHE BOOL "Set by speechwarp_listen" FORCE)
add_subdirectory(${whisper_dir} whisper.cpp EXCLUDE_FROM_ALL)

# The static libraries whisper.cpp built, in link order.
set(listen_engine_targets whisper)
foreach(target ggml ggml-cpu ggml-blas ggml-metal ggml-base)
  if(TARGET ${target})
    list(APPEND listen_engine_targets ${target})
  endif()
endforeach()

# What a program linking the static library needs besides it.
find_package(Threads REQUIRED)
set(listen_system_libraries Threads::Threads)
if(APPLE)
  list(APPEND listen_system_libraries "-framework Accelerate" "-framework Foundation")
  if(SPEECHWARP_LISTEN_METAL)
    list(APPEND listen_system_libraries "-framework Metal" "-framework MetalKit")
  endif()
elseif(UNIX)
  list(APPEND listen_system_libraries m ${CMAKE_DL_LIBS})
endif()

# ---- The exported names, read from the header, for the linker.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/include/speechwarp_listen.h listen_header)
string(REGEX MATCHALL "speechwarp_listen_[a-z0-9_]+\\(" listen_api "${listen_header}")
list(TRANSFORM listen_api REPLACE "\\($" "")
list(REMOVE_DUPLICATES listen_api)
set(listen_macos_exports "")
set(listen_version_script "{\n  global:\n")
foreach(name ${listen_api})
  string(APPEND listen_macos_exports "_${name}\n")
  string(APPEND listen_version_script "    ${name};\n")
endforeach()
string(APPEND listen_version_script "  local: *;\n};\n")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/exports.txt "${listen_macos_exports}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/exports.map "${listen_version_script}")

set(listen_sources src/listen.cpp src/catalogue.cpp src/resample.cpp)

function(listen_configure target)
  target_include_directories(${target}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE ${listen_root}/include src)
  target_compile_features(${target} PRIVATE cxx_std_17)
  set_target_properties(${target} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W3)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

# ---- Shared library: whisper.cpp and ggml linked in, only speechwarp_listen_* exported.
if(SPEECHWARP_LISTEN_BUILD_SHARED)
  add_library(speechwarp_listen_shared SHARED ${listen_sources})
  listen_configure(speechwarp_listen_shared)
  target_compile_definitions(speechwarp_listen_shared PRIVATE SPEECHWARP_LISTEN_BUILD
                             INTERFACE SPEECHWARP_LISTEN_SHARED)
  target_link_libraries(speechwarp_listen_shared PRIVATE whisper)
  set_target_properties(speechwarp_listen_shared PROPERTIES
    OUTPUT_NAME speechwarp_listen
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
  if(APPLE)
    target_link_options(speechwarp_listen_shared PRIVATE
      "LINKER:-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/exports.txt" "LINKER:-dead_strip")
  elseif(UNIX)
    target_link_options(speechwarp_listen_shared PRIVATE
      "LINKER:--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map" "LINKER:--exclude-libs,ALL"
      "LINKER:--gc-sections")
  endif()
endif()

# ---- Static library. On Apple and ELF platforms everything (ours, whisper.cpp, ggml) is linked into one
# relocatable object whose only global symbols are speechwarp_listen_*, so it links next to another copy of
# whisper.cpp or ggml without clashing. Elsewhere (MSVC) it is a plain static library of our code, and the
# engine's libraries are linked as dependencies.
if(SPEECHWARP_LISTEN_BUILD_STATIC)
  add_library(speechwarp_listen_objects OBJECT ${listen_sources})
  listen_configure(speechwarp_listen_objects)
  # Only for whisper.h and ggml.h: an object library is not linked.
  target_link_libraries(speechwarp_listen_objects PRIVATE whisper)

  set(listen_static_file ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}speechwarp_listen${CMAKE_STATIC_LIBRARY_SUFFIX})
  set(listen_combined ${CMAKE_CURRENT_BINARY_DIR}/speechwarp_listen_combined.o)
  set(listen_engine_files)
  foreach(target ${listen_engine_targets})
    list(APPEND listen_engine_files $<TARGET_FILE:${target}>)
  endforeach()

  if(APPLE OR (UNIX AND CMAKE_OBJCOPY))
    if(APPLE)
      set(listen_whole)
      foreach(file ${listen_engine_files})
        list(APPEND listen_whole "-Wl,-force_load,${file}")
      endforeach()
      add_custom_command(OUTPUT ${listen_static_file}
        COMMAND ${CMAKE_CXX_COMPILER} -r -nostdlib -o ${listen_combined}
                $<TARGET_OBJECTS:speechwarp_listen_objects> ${listen_whole}
                -Wl,-exported_symbols_list,${CMAKE_CURRENT_BINARY_DIR}/exports.txt
        COMMAND ${CMAKE_COMMAND} -E rm -f ${listen_static_file}
        COMMAND libtool -static -no_warning_for_no_symbols -o ${listen_static_file} ${listen_combined}
        DEPENDS speechwarp_listen_objects ${listen_engine_targets} $<TARGET_OBJECTS:speechwarp_listen_objects>
        COMMAND_EXPAND_LISTS
        COMMENT "Linking the static speechwarp_listen with whisper.cpp hidden inside")
    else()
      add_custom_command(OUTPUT ${listen_static_file}
        COMMAND ${CMAKE_CXX_COMPILER} -r -nostdlib -o ${listen_combined}
                $<TARGET_OBJECTS:speechwarp_listen_objects>
                -Wl,--whole-archive ${listen_engine_files} -Wl,--no-whole-archive
        COMMAND ${CMAKE_OBJCOPY} --localize-hidden ${listen_combined}
        COMMAND ${CMAKE_COMMAND} -E rm -f ${listen_static_file}
        COMMAND ${CMAKE_AR} qc ${listen_static_file} ${listen_combined}
        COMMAND ${CMAKE_RANLIB} ${listen_static_file}
        DEPENDS speechwarp_listen_objects ${listen_engine_targets} $<TARGET_OBJECTS:speechwarp_listen_objects>
        COMMAND_EXPAND_LISTS
        COMMENT "Linking the static speechwarp_listen with whisper.cpp hidden inside")
    endif()
    add_custom_target(speechwarp_listen_static_build ALL DEPENDS ${listen_static_file})
    add_library(speechwarp_listen_static STATIC IMPORTED GLOBAL)
    set_target_properties(speechwarp_listen_static PROPERTIES
      IMPORTED_LOCATION ${listen_static_file}
      IMPORTED_LINK_INTERFACE_LANGUAGES CXX
      INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include
      INTERFACE_LINK_LIBRARIES "${listen_system_libraries}")
    add_dependencies(speechwarp_listen_static speechwarp_listen_static_build)
    set(listen_static_combined ON)
  else()
    add_library(speechwarp_listen_static STATIC $<TARGET_OBJECTS:speechwarp_listen_objects>)
    target_include_directories(speechwarp_listen_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(speechwarp_listen_static PUBLIC whisper)
    set_target_properties(speechwarp_listen_static PROPERTIES OUTPUT_NAME speechwarp_listen_static)
    set(listen_static_combined OFF)
  endif()
endif()

if(SPEECHWARP_LISTEN_BUILD_SHARED)
  set(listen_link_target speechwarp_listen_shared)
else()
  set(listen_link_target speechwarp_listen_static)
endif()

if(SPEECHWARP_LISTEN_BUILD_TOOLS)
  add_executable(speechwarp_transcribe tools/speechwarp-transcribe.c)
  set_target_properties(speechwarp_transcribe PROPERTIES OUTPUT_NAME speechwarp-transcribe C_STANDARD 99)
  if(SPEECHWARP_LISTEN_BUILD_STATIC)
    target_link_libraries(speechwarp_transcribe PRIVATE speechwarp_listen_static)
  else()
    target_link_libraries(speechwarp_transcribe PRIVATE speechwarp_listen_shared)
  endif()
  if(NOT MSVC)
    target_compile_options(speechwarp_transcribe PRIVATE -Wall -Wextra)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(speechwarp_transcribe PRIVATE m)
  endif()
endif()

if(SPEECHWARP_LISTEN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(SPEECHWARP_LISTEN_INSTALL)
  include(GNUInstallDirs)
  install(FILES include/speechwarp_listen.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  if(SPEECHWARP_LISTEN_BUILD_SHARED)
    install(TARGETS speechwarp_listen_shared
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
  endif()
  if(SPEECHWARP_LISTEN_BUILD_STATIC AND listen_static_combined)
    install(FILES ${listen_static_file} DESTINATION ${CMAKE_INSTALL_LIBDIR})
  endif()
  if(SPEECHWARP_LISTEN_BUILD_TOOLS)
    install(TARGETS speechwarp_transcribe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()
endif()
