add_executable(test_stream test_stream.c)
target_link_libraries(test_stream PRIVATE speechwarp_static)
target_include_directories(test_stream PRIVATE ../src)
add_test(NAME stream COMMAND test_stream)

add_executable(test_fft test_fft.c)
target_link_libraries(test_fft PRIVATE speechwarp_static)
target_include_directories(test_fft PRIVATE ../src)
add_test(NAME fft COMMAND test_fft)

# Compares the library with upstream's own shim, built here from the untouched sources. The two can be linked
# together because the library's copies of Sonic and Speedy are renamed (src/rename.h).
add_executable(test_parity test_parity.c
  ../third_party/sonic/sonic.c
  ../third_party/speedy/speedy.c
  ../third_party/speedy/soniclib.c
  ../third_party/kissfft/kiss_fft.c)
target_compile_definitions(test_parity PRIVATE SONIC_INTERNAL KISS_FFT)
target_include_directories(test_parity PRIVATE ../src ../third_party/sonic ../third_party/speedy
                                               ../third_party/kissfft)
target_link_libraries(test_parity PRIVATE speechwarp_static)
if(speechwarp_has_libm)
  target_link_libraries(test_stream PRIVATE m)
  target_link_libraries(test_fft PRIVATE m)
  target_link_libraries(test_parity PRIVATE m)
endif()
if(MSVC)
  target_compile_options(test_parity PRIVATE /w)
else()
  target_compile_options(test_parity PRIVATE -w)
endif()
add_test(NAME parity COMMAND test_parity)

# Only speechwarp_* may be visible: Android ships its own libsonic, and other libraries embed Sonic and
# KISS FFT too.
find_program(SPEECHWARP_NM nm)
if(SPEECHWARP_NM AND NOT WIN32)
  add_test(NAME symbols_static COMMAND ${CMAKE_COMMAND}
    -DNM=${SPEECHWARP_NM} -DLIBRARY=$<TARGET_FILE:speechwarp_static> -DKIND=static
    -DHEADER=${PROJECT_SOURCE_DIR}/include/speechwarp.h -P ${CMAKE_CURRENT_SOURCE_DIR}/check_symbols.cmake)
  if(SPEECHWARP_BUILD_SHARED)
    add_test(NAME symbols_shared COMMAND ${CMAKE_COMMAND}
      -DNM=${SPEECHWARP_NM} -DLIBRARY=$<TARGET_FILE:speechwarp_shared> -DKIND=shared
      -DHEADER=${PROJECT_SOURCE_DIR}/include/speechwarp.h -P ${CMAKE_CURRENT_SOURCE_DIR}/check_symbols.cmake)
  endif()
endif()

if(SPEECHWARP_BUILD_TOOLS)
  add_executable(make_wav make_wav.c)
  if(speechwarp_has_libm)
    target_link_libraries(make_wav PRIVATE m)
  endif()
  add_test(NAME tool COMMAND ${CMAKE_COMMAND}
    -DTOOL=$<TARGET_FILE:speechwarp_tool> -DMAKE_WAV=$<TARGET_FILE:make_wav> -DDIR=${CMAKE_CURRENT_BINARY_DIR}
    -P ${CMAKE_CURRENT_SOURCE_DIR}/check_tool.cmake)
endif()
