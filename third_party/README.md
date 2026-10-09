# Third-party sources

Unmodified copies of the upstream files this library is built from. Each folder keeps its licence.

| Folder | Upstream | Commit | Licence |
|--------|----------|--------|---------|
| `speedy/` | https://github.com/google/speedy | `e05c9b6fa473881e9c2b5ddaa323435b5513012c` | Apache-2.0 |
| `sonic/` | https://github.com/waywardgeek/sonic | `b93885dcb70aae50c6f76b0fe4e0868f029a077e` | Apache-2.0 |
| `kissfft/` | https://github.com/mborgerding/kissfft | `e5e3fac46e0d94a8f8170c06706b7a4218828333` | BSD-3-Clause |
| `whisper.cpp/` | https://github.com/ggml-org/whisper.cpp | `d1be6fde11ac6e0407606b4e42fe72d34add8037` (tag `v1.9.5`) | MIT (includes ggml, MIT) |

Only the files needed to build are copied: no tests, tools or build scripts. To update, copy the same files
from a newer commit and change the hash here. Keep local changes out of these folders; put fixes upstream or
in `src/`.

Build notes: nothing here is compiled directly. Each `src/third_party_*.c` includes one of these files after
`src/rename.h`, which prefixes its external symbols so the library can be linked next to another copy of Sonic
or KISS FFT. `sonic/sonic.c` is built with `SONIC_INTERNAL`, as Speedy expects, and `speedy/speedy.c` with
`KISS_FFT` (the alternative is FFTW).

`speedy/soniclib.c` and `speedy/sonic2.h` are upstream's shim between Speedy and Sonic. The library uses
`src/speechwarp.c` in its place; the shim is kept as the reference that `tests/test_parity.c` compares against.

## whisper.cpp (for `listen/` only)

`whisper.cpp/` is a git submodule, not a copy, and only the optional speech-to-text module in `listen/` uses it:
the speechwarp library and its bindings never compile it. It is the whole upstream tree at the tag above (about
40 MB, of which `listen/CMakeLists.txt` builds `src/` and `ggml/`), with its MIT licence in
`whisper.cpp/LICENSE`.

The submodule is marked `update = none` in `.gitmodules`, so `git clone --recursive`, `git submodule update
--init` and package managers that fetch submodules (Swift Package Manager, Cargo git dependencies) skip it,
and nobody who only wants speechwarp downloads it. Fetch it explicitly to build `listen/`:

    git submodule update --init --checkout third_party/whisper.cpp

To update it, check out a newer tag inside the submodule, commit the new pointer, and change the commit here.
The Python source distribution prunes it (`MANIFEST.in`), and the other packages list their files explicitly.
