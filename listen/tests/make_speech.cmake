# Makes the speech the tests recognise, with macOS's `say`. Without it, writes nothing: the tests that need
# speech then skip.
#   sentence.wav  one sentence, Samantha, 16-bit PCM at 22.05 kHz
#   passage.wav   passage.txt, Daniel, 32-bit float at 44.1 kHz, about two minutes
#   ending.wav    passage.txt from "He set the radio" on, Samantha, the same format, about 50 seconds
# The test joins the last two into one recording of nearly three minutes with two voices.
file(MAKE_DIRECTORY ${DIR})
if(NOT SAY)
  message(STATUS "say not found: no speech made, so the tests that need it will skip")
  return()
endif()

function(say voice format text_or_file output)
  if(EXISTS ${DIR}/${output})
    return()
  endif()
  if(text_or_file MATCHES "\\.txt$")
    set(input -f ${text_or_file})
  else()
    set(input ${text_or_file})
  endif()
  execute_process(COMMAND ${SAY} -v ${voice} -o ${DIR}/${output}.tmp.wav --file-format=WAVE --data-format=${format}
                          ${input}
                  RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "say failed for ${output}")
  endif()
  file(RENAME ${DIR}/${output}.tmp.wav ${DIR}/${output})
endfunction()

say(Samantha LEI16@22050 "The quick brown fox jumps over the lazy dog, and the farmer laughs." sentence.wav)
say(Daniel LEF32@44100 ${PASSAGE} passage.wav)
file(READ ${PASSAGE} text)
string(FIND "${text}" "He set the radio" at)
string(SUBSTRING "${text}" ${at} -1 ending)
say(Samantha LEF32@44100 "${ending}" ending.wav)
