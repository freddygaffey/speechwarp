/* Sonic, built as Speedy expects it: SONIC_INTERNAL renames its API to sonicInt*. */
#include "rename.h"
#define SONIC_INTERNAL 1
#include "../third_party/sonic/sonic.c"
