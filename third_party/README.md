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

Build notes: compile `sonic/sonic.c` with `-DSONIC_INTERNAL` (Speedy's `soniclib.c` re-exports Sonic's public
names on top of it) and `speedy/*.c` with `-DKISS_FFT` (the alternative is FFTW).
