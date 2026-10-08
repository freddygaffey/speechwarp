# Builds the speechwarp library for Android, Linux and Windows apps. iOS and macOS build the same sources
# through CocoaPods (see ../ios and ../macos).
cmake_minimum_required(VERSION 3.10)
project(speechwarp_library LANGUAGES C)

# The C sources are in speechwarp/ in a published package, where scripts/vendor.sh copied them to, laid out as
# at the top of the repository. In the repository, without that copy, the sources at its top are used.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/speechwarp/include/speechwarp.h")
  set(root "${CMAKE_CURRENT_SOURCE_DIR}/speechwarp")
else()
  set(root "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
endif()

add_library(speechwarp SHARED
  "${root}/src/speechwarp.c"
  "${root}/src/trainer.c"
  "${root}/src/fft.c"
  "${root}/src/third_party_kissfft.c"
  "${root}/src/third_party_sonic.c"
  "${root}/src/third_party_speedy.c"
)
target_include_directories(speechwarp PRIVATE "${root}/include" "${root}/third_party/kissfft")
set_target_properties(speechwarp PROPERTIES OUTPUT_NAME "speechwarp" C_VISIBILITY_PRESET hidden)
# SPEECHWARP_BUILD exports the API on Windows. NDEBUG: upstream is full of assertions.
target_compile_definitions(speechwarp PRIVATE SPEECHWARP_BUILD NDEBUG)

if (ANDROID)
  # Support Android 15 16k page size.
  target_link_options(speechwarp PRIVATE "-Wl,-z,max-page-size=16384")
  target_link_libraries(speechwarp PRIVATE m)
elseif (UNIX)
  target_link_libraries(speechwarp PRIVATE m)
endif()
