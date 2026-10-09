"""The functions of listen/include/speechwarp_listen.h, through ctypes."""
import ctypes
import glob
import os
from ctypes import POINTER, c_char_p, c_double, c_float, c_int, c_int64, c_void_p

OK = 0
ERROR_ARGUMENT = -1
ERROR_MEMORY = -2
ERROR_ENGINE = -3
ERROR_CANCELLED = -4
ERROR_FINISHED = -5

PRESET_FAST = 0
PRESET_BALANCED = 1
PRESET_ACCURATE = 2

LOAD_GPU = 1


def _load():
    here = os.path.dirname(os.path.abspath(__file__))
    names = ("libspeechwarp_listen.so", "libspeechwarp_listen.dylib", "speechwarp_listen.dll",
             "libspeechwarp_listen.dll")
    for name in names:
        path = os.path.join(here, name)
        if os.path.exists(path):
            return ctypes.CDLL(path)
    found = glob.glob(os.path.join(here, "*speechwarp_listen*"))
    raise OSError(f"the speechwarp_listen library is missing from {here} (found {found})")


lib = _load()


def _declare(name, result, *arguments):
    function = getattr(lib, name)
    function.restype = result
    function.argtypes = list(arguments)
    return function


# About the library
version = _declare("speechwarp_listen_version", c_char_p)
engine_version = _declare("speechwarp_listen_engine_version", c_char_p)
system_info = _declare("speechwarp_listen_system_info", c_char_p)
error_message = _declare("speechwarp_listen_error_message", c_char_p, c_int)
set_log = _declare("speechwarp_listen_set_log", None, c_int)

# Catalogue
catalogue_count = _declare("speechwarp_listen_catalogue_count", c_int)
catalogue_find = _declare("speechwarp_listen_catalogue_find", c_int, c_char_p)
catalogue_id = _declare("speechwarp_listen_catalogue_id", c_char_p, c_int)
catalogue_name = _declare("speechwarp_listen_catalogue_name", c_char_p, c_int)
catalogue_languages = _declare("speechwarp_listen_catalogue_languages", c_char_p, c_int)
catalogue_size = _declare("speechwarp_listen_catalogue_size", c_int64, c_int)
catalogue_file_name = _declare("speechwarp_listen_catalogue_file_name", c_char_p, c_int)
catalogue_url = _declare("speechwarp_listen_catalogue_url", c_char_p, c_int)
catalogue_sha256 = _declare("speechwarp_listen_catalogue_sha256", c_char_p, c_int)
catalogue_relative_speed = _declare("speechwarp_listen_catalogue_relative_speed", c_double, c_int)

# Models
model_load = _declare("speechwarp_listen_model_load", c_void_p, c_char_p, c_int, c_int)
model_free = _declare("speechwarp_listen_model_free", None, c_void_p)
model_multilingual = _declare("speechwarp_listen_model_multilingual", c_int, c_void_p)
model_type = _declare("speechwarp_listen_model_type", c_char_p, c_void_p)

# Options
options_create = _declare("speechwarp_listen_options_create", c_void_p)
options_free = _declare("speechwarp_listen_options_free", None, c_void_p)
options_set_language = _declare("speechwarp_listen_options_set_language", c_int, c_void_p, c_char_p)
options_set_word_timestamps = _declare("speechwarp_listen_options_set_word_timestamps", None, c_void_p, c_int)
options_set_prompt = _declare("speechwarp_listen_options_set_prompt", None, c_void_p, c_char_p)
options_set_preset = _declare("speechwarp_listen_options_set_preset", None, c_void_p, c_int)
options_set_threads = _declare("speechwarp_listen_options_set_threads", None, c_void_p, c_int)
options_cancel = _declare("speechwarp_listen_options_cancel", None, c_void_p)

# A passage
transcribe = _declare("speechwarp_listen_transcribe", c_void_p, c_void_p, POINTER(c_float), c_int64, c_int, c_void_p,
                      POINTER(c_int))

# Results
result_free = _declare("speechwarp_listen_result_free", None, c_void_p)
result_segment_count = _declare("speechwarp_listen_result_segment_count", c_int, c_void_p)
result_text = _declare("speechwarp_listen_result_text", c_char_p, c_void_p)
result_language = _declare("speechwarp_listen_result_language", c_char_p, c_void_p)
result_segment_text = _declare("speechwarp_listen_result_segment_text", c_char_p, c_void_p, c_int)
result_segment_start = _declare("speechwarp_listen_result_segment_start", c_double, c_void_p, c_int)
result_segment_end = _declare("speechwarp_listen_result_segment_end", c_double, c_void_p, c_int)
result_word_count = _declare("speechwarp_listen_result_word_count", c_int, c_void_p, c_int)
result_word_text = _declare("speechwarp_listen_result_word_text", c_char_p, c_void_p, c_int, c_int)
result_word_start = _declare("speechwarp_listen_result_word_start", c_double, c_void_p, c_int, c_int)
result_word_end = _declare("speechwarp_listen_result_word_end", c_double, c_void_p, c_int, c_int)
result_word_probability = _declare("speechwarp_listen_result_word_probability", c_double, c_void_p, c_int, c_int)

# Sessions
session_create = _declare("speechwarp_listen_session_create", c_void_p, c_void_p, c_int, c_void_p)
session_free = _declare("speechwarp_listen_session_free", None, c_void_p)
session_write = _declare("speechwarp_listen_session_write", c_int, c_void_p, POINTER(c_float), c_int64)
session_process = _declare("speechwarp_listen_session_process", c_int, c_void_p)
session_partial = _declare("speechwarp_listen_session_partial", c_void_p, c_void_p, POINTER(c_int))
session_finish = _declare("speechwarp_listen_session_finish", c_int, c_void_p)
session_take = _declare("speechwarp_listen_session_take", c_void_p, c_void_p)
session_cancel = _declare("speechwarp_listen_session_cancel", None, c_void_p)
session_seconds_written = _declare("speechwarp_listen_session_seconds_written", c_double, c_void_p)
session_seconds_recognised = _declare("speechwarp_listen_session_seconds_recognised", c_double, c_void_p)
