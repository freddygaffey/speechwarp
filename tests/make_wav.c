/* Writes the WAV files the command-line tool is tested with: make_wav plain.wav extensible.wav
 * The first is mono 16-bit PCM. The second holds the same audio as stereo 32-bit float, marked
 * WAVE_FORMAT_EXTENSIBLE and with an extra chunk before the data, as macOS afconvert writes them. */
#include <string.h>

#include "test_util.h"

#define RATE 16000

static void put(FILE* file, uint32_t value, int bytes) {
  while (bytes--) {
    fputc((int)(value & 255), file);
    value >>= 8;
  }
}

int main(int argc, char** argv) {
  static const unsigned char float_guid_tail[14] = {0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xAA, 0, 0x38, 0x9B, 0x71};
  int frames, i;
  int16_t* speech = make_speech(RATE, 10, 11, &frames);
  FILE* file;

  if (argc != 3) return 2;

  file = fopen(argv[1], "wb");
  if (!file) return 1;
  fwrite("RIFF", 1, 4, file);
  put(file, 36 + (uint32_t)frames * 2, 4);
  fwrite("WAVEfmt ", 1, 8, file);
  put(file, 16, 4);
  put(file, 1, 2);
  put(file, 1, 2);
  put(file, RATE, 4);
  put(file, RATE * 2, 4);
  put(file, 2, 2);
  put(file, 16, 2);
  fwrite("data", 1, 4, file);
  put(file, (uint32_t)frames * 2, 4);
  for (i = 0; i < frames; i++) put(file, (uint16_t)speech[i], 2);
  fclose(file);

  file = fopen(argv[2], "wb");
  if (!file) return 1;
  fwrite("RIFF", 1, 4, file);
  put(file, 4 + 48 + 12 + 8 + (uint32_t)frames * 8, 4);
  fwrite("WAVEfmt ", 1, 8, file);
  put(file, 40, 4);
  put(file, 0xFFFE, 2);
  put(file, 2, 2);
  put(file, RATE, 4);
  put(file, RATE * 8, 4);
  put(file, 8, 2);
  put(file, 32, 2);
  put(file, 22, 2);
  put(file, 32, 2);
  put(file, 3, 4);
  put(file, 3, 2); /* float */
  fwrite(float_guid_tail, 1, sizeof float_guid_tail, file);
  fwrite("FLLR", 1, 4, file);
  put(file, 3, 4); /* an odd size, so it is followed by a padding byte */
  fwrite("\0\0\0\0", 1, 4, file);
  fwrite("data", 1, 4, file);
  put(file, (uint32_t)frames * 8, 4);
  for (i = 0; i < frames; i++) {
    float value = speech[i] / 32768.0f;
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    put(file, bits, 4);
    put(file, bits, 4);
  }
  fclose(file);
  free(speech);
  return 0;
}
