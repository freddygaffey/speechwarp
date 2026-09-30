# Third-party sources

Unmodified copies of the upstream files this library is built from. Each folder keeps its licence.

| Folder | Upstream | Commit | Licence |
|--------|----------|--------|---------|
| `speedy/` | https://github.com/google/speedy | `e05c9b6fa473881e9c2b5ddaa323435b5513012c` | Apache-2.0 |
| `sonic/` | https://github.com/waywardgeek/sonic | `b93885dcb70aae50c6f76b0fe4e0868f029a077e` | Apache-2.0 |
| `kissfft/` | https://github.com/mborgerding/kissfft | `e5e3fac46e0d94a8f8170c06706b7a4218828333` | BSD-3-Clause |

Only the files needed to build are copied: no tests, tools or build scripts. To update, copy the same files
from a newer commit and change the hash here. Keep local changes out of these folders; put fixes upstream or
in `src/`.

Build notes: nothing here is compiled directly. Each `src/third_party_*.c` includes one of these files after
`src/rename.h`, which prefixes its external symbols so the library can be linked next to another copy of Sonic
or KISS FFT. `sonic/sonic.c` is built with `SONIC_INTERNAL`, as Speedy expects, and `speedy/speedy.c` with
`KISS_FFT` (the alternative is FFTW).

`speedy/soniclib.c` and `speedy/sonic2.h` are upstream's shim between Speedy and Sonic. The library uses
`src/speechwarp.c` in its place; the shim is kept as the reference that `tests/test_parity.c` compares against.
