cmake_minimum_required(VERSION 3.13)
project(Speechwarp C CXX)

# The C library's sources, copied into the package by scripts/vendor.sh.
set(speechwarp ../cpp/speechwarp)

add_library(
    react-native-speechwarp    STATIC
    ../cpp/SpeechwarpImpl.cpp
    ${speechwarp}/src/speechwarp.c
    ${speechwarp}/src/trainer.c
    ${speechwarp}/src/fft.c
    ${speechwarp}/src/third_party_kissfft.c
    ${speechwarp}/src/third_party_sonic.c
    ${speechwarp}/src/third_party_speedy.c
)

set_target_properties(
    react-native-speechwarp PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    C_STANDARD 99
)

# Upstream is full of assertions, and its warnings are not ours to fix.
target_compile_definitions(react-native-speechwarp PRIVATE NDEBUG)
set_source_files_properties(
    ${speechwarp}/src/third_party_kissfft.c
    ${speechwarp}/src/third_party_sonic.c
    ${speechwarp}/src/third_party_speedy.c
    PROPERTIES COMPILE_OPTIONS "-w"
)

target_include_directories(
    react-native-speechwarp    PUBLIC
    ../cpp
)
target_include_directories(
    react-native-speechwarp    PRIVATE
    ${speechwarp}/include
    ${speechwarp}/third_party/kissfft
)

target_link_libraries(
    react-native-speechwarp    jsi
    reactnative
    react_codegen_SpeechwarpSpec
)
