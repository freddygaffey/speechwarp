# Runs speechwarp-transcribe on the test sentence: text, SubRip and word listings. Skips without a model
# (SPEECHWARP_LISTEN_MODEL) or without the speech.
if(NOT DEFINED ENV{SPEECHWARP_LISTEN_MODEL} OR NOT EXISTS ${DIR}/sentence.wav)
  message("SKIPPED: set SPEECHWARP_LISTEN_MODEL and run on macOS (for say)")
  return()
endif()
set(model $ENV{SPEECHWARP_LISTEN_MODEL})

function(run expect)
  execute_process(COMMAND ${TOOL} ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "speechwarp-transcribe ${ARGN} failed (${result}):\n${err}")
  endif()
  string(TOLOWER "${out}" lower)
  if(NOT lower MATCHES "${expect}")
    message(FATAL_ERROR "speechwarp-transcribe ${ARGN}: expected ${expect} in:\n${out}")
  endif()
  message(STATUS "speechwarp-transcribe ${ARGN}:\n${out}")
endfunction()

run("brown fox" ${model} ${DIR}/sentence.wav --language en)
run("1\n00:00:0[0-9],[0-9][0-9][0-9] --> 00:00:0[0-9],[0-9][0-9][0-9]\n[^\n]*fox" ${model} ${DIR}/sentence.wav --srt)
run("[0-9.]+ +[0-9.]+ +[01]\\.[0-9][0-9] +fox" ${model} ${DIR}/sentence.wav --words)
run("brown fox" ${model} ${DIR}/sentence.wav --once --preset accurate)

execute_process(COMMAND ${TOOL} ${model} ${DIR}/missing.wav RESULT_VARIABLE result OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
  message(FATAL_ERROR "speechwarp-transcribe succeeded on a missing file")
endif()
