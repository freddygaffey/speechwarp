# Builds libspeechwarp_jni: the library's C sources and the JNI layer, in one shared library. Gradle runs this
# for each Android ABI, and once more for this computer so the unit tests can run on its JVM.
cmake_minimum_required(VERSION 3.16)
project(speechwarp_jni C)

set(root ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..)

add_library(speechwarp_jni SHARED
  speechwarp_jni.c
  ${root}/src/speechwarp.c
  ${root}/src/fft.c
  ${root}/src/trainer.c
  ${root}/src/third_party_kissfft.c
  ${root}/src/third_party_sonic.c
  ${root}/src/third_party_speedy.c)
target_include_directories(speechwarp_jni PRIVATE ${root}/include ${root}/third_party/kissfft)
# Upstream is full of assertions.
target_compile_definitions(speechwarp_jni PRIVATE NDEBUG)
set_target_properties(speechwarp_jni PROPERTIES C_STANDARD 99 C_VISIBILITY_PRESET hidden)
set_source_files_properties(
  ${root}/src/third_party_kissfft.c ${root}/src/third_party_sonic.c ${root}/src/third_party_speedy.c
  PROPERTIES COMPILE_OPTIONS "-w")

if(ANDROID)
  # Android 15 devices may use 16 KB pages; NDK 27 and older need to be told.
  target_link_options(speechwarp_jni PRIVATE "-Wl,-z,max-page-size=16384")
  target_link_libraries(speechwarp_jni PRIVATE m)
else()
  find_package(JNI REQUIRED)
  target_include_directories(speechwarp_jni PRIVATE ${JNI_INCLUDE_DIRS})
endif()
