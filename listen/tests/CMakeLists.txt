# Tests for speechwarp_listen. Those that recognise speech need a model and speech to recognise:
#   SPEECHWARP_LISTEN_MODEL  path to a whisper.cpp model, e.g. ggml-tiny.en.bin (about 75 MB); without it
#                            they are skipped
#   the speech is made with macOS's `say` when the tests run; without `say` they are skipped too.
# No model and no audio are kept in the repository.

add_executable(test_listen_catalogue test_catalogue.c)
target_link_libraries(test_listen_catalogue PRIVATE ${listen_link_target})
target_include_directories(test_listen_catalogue PRIVATE ${listen_root}/include)
add_test(NAME listen_catalogue COMMAND test_listen_catalogue)

# The resampler is internal, so its test compiles it directly.
add_executable(test_listen_resample test_resample.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../src/resample.cpp)
target_include_directories(test_listen_resample PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_features(test_listen_resample PRIVATE cxx_std_17)
add_test(NAME listen_resample COMMAND test_listen_resample)

# Speech for the tests: a sentence, and a passage of about two and a half minutes in another voice and rate.
find_program(SPEECHWARP_SAY say)
set(speech_dir ${CMAKE_CURRENT_BINARY_DIR}/speech)
add_test(NAME listen_make_speech COMMAND ${CMAKE_COMMAND}
  -DSAY=${SPEECHWARP_SAY} -DDIR=${speech_dir} -DPASSAGE=${CMAKE_CURRENT_SOURCE_DIR}/passage.txt
  -P ${CMAKE_CURRENT_SOURCE_DIR}/make_speech.cmake)
set_tests_properties(listen_make_speech PROPERTIES FIXTURES_SETUP listen_speech)

find_package(Threads REQUIRED)
add_executable(test_listen test_listen.cpp)
target_link_libraries(test_listen PRIVATE ${listen_link_target} Threads::Threads)
target_compile_features(test_listen PRIVATE cxx_std_17)
add_test(NAME listen_speech COMMAND test_listen ${speech_dir} ${CMAKE_CURRENT_SOURCE_DIR}/passage.txt)
set_tests_properties(listen_speech PROPERTIES FIXTURES_REQUIRED listen_speech SKIP_RETURN_CODE 77)

if(SPEECHWARP_LISTEN_BUILD_TOOLS)
  add_test(NAME listen_tool COMMAND ${CMAKE_COMMAND}
    -DTOOL=$<TARGET_FILE:speechwarp_transcribe> -DDIR=${speech_dir} -P ${CMAKE_CURRENT_SOURCE_DIR}/check_tool.cmake)
  set_tests_properties(listen_tool PROPERTIES FIXTURES_REQUIRED listen_speech SKIP_RETURN_CODE 77
                       SKIP_REGULAR_EXPRESSION "SKIPPED:")
endif()

# Only speechwarp_listen_* may be visible: an app may hold another copy of whisper.cpp or ggml.
find_program(SPEECHWARP_LISTEN_NM nm)
if(SPEECHWARP_LISTEN_NM AND NOT WIN32)
  if(SPEECHWARP_LISTEN_BUILD_SHARED)
    add_test(NAME listen_symbols_shared COMMAND ${CMAKE_COMMAND}
      -DNM=${SPEECHWARP_LISTEN_NM} -DLIBRARY=$<TARGET_FILE:speechwarp_listen_shared> -DKIND=shared
      -DHEADER=${CMAKE_CURRENT_SOURCE_DIR}/../include/speechwarp_listen.h
      -P ${CMAKE_CURRENT_SOURCE_DIR}/check_symbols.cmake)
  endif()
  if(SPEECHWARP_LISTEN_BUILD_STATIC AND listen_static_combined)
    add_test(NAME listen_symbols_static COMMAND ${CMAKE_COMMAND}
      -DNM=${SPEECHWARP_LISTEN_NM} -DLIBRARY=${listen_static_file} -DKIND=static
      -DHEADER=${CMAKE_CURRENT_SOURCE_DIR}/../include/speechwarp_listen.h
      -P ${CMAKE_CURRENT_SOURCE_DIR}/check_symbols.cmake)
  endif()
endif()
