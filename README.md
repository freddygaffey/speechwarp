# speechwarp

Nonlinear speed-up for speech: listen faster and still follow it.

Ordinary speed-up compresses everything by the same amount. People who talk fast do not: they hurry through
vowels and pauses and keep consonants, which carry most of the meaning, close to their normal length.
speechwarp does the same, so speech stays easier to follow at high speeds.

It packages Google's [Speedy](https://github.com/google/speedy) algorithm (a reimplementation of MACH1:
Covell, Withgott and Slaney, ICASSP 1998) together with the [Sonic](https://github.com/waywardgeek/sonic)
library it drives, behind one small C API, with bindings for other languages.

**This is not an official Google product.** It redistributes their Apache-2.0 code; see [NOTICE](NOTICE).

## Status

Early. What exists today:

- `third_party/` - the upstream sources, pinned (see `third_party/README.md`)
- `include/speechwarp.h` - the planned C API (not implemented yet)
- `examples/blind-ab-test/` - a blind listening test comparing even and nonlinear speed-up on your own audio

Planned: the C library and tests, a command-line tool, then bindings for C#/.NET, Python, JavaScript
(WebAssembly), Android and Apple platforms.

## Licence

Apache-2.0. Third-party code keeps its own licence: see `NOTICE` and `third_party/`.
