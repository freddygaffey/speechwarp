# Contributing

## Layout

| Path | What is there |
|------|---------------|
| `include/speechwarp.h` | The public C API, and the version number. |
| `src/` | The library: `speechwarp.c`, `fft.c`, and one small file per upstream source. |
| `third_party/` | Upstream sources, unmodified. See `third_party/README.md`. |
| `tests/` | C tests, run by `ctest`. |
| `tools/` | The `speechwarp` command-line tool. |
| `bindings/` | One folder per language. |
| `examples/`, `docs/` | What they say. |
| `Package.swift`, `pyproject.toml`, `setup.py`, `MANIFEST.in`, `Cargo.toml`, `go.mod` | At the top because each of those package managers can only build what is under its own manifest, and the bindings compile the C sources. They belong to `bindings/swift`, `bindings/python`, `bindings/rust` and `bindings/go`. |

## Building and testing

| Part | Commands | Needs |
|------|----------|-------|
| C library, tool, example | `cmake -B build && cmake --build build && ctest --test-dir build` | CMake 3.16, a C compiler |
| The same under sanitizers | `cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address,undefined" -DCMAKE_C_FLAGS_DEBUG="-O1 -g"`, then build and `ctest` | clang or gcc |
| C# | `bindings/dotnet/build-native.sh && (cd bindings/dotnet && dotnet test --project Speechwarp.Tests)` | .NET 10 SDK |
| Python | `pip install ".[test]" && pytest` | Python 3.9 |
| Swift | `swift test` | Swift 5.7 |
| Kotlin | `(cd bindings/android && ./gradlew :speechwarp:testReleaseUnitTest :speechwarp:assembleRelease)` | JDK 17 or later, Android SDK with NDK 27.1.12297006, `cmake` on the path |
| JavaScript | `(cd bindings/js && npm install && npm run build && npm test)` | Node 20, Emscripten |
| Rust | `cargo test` | Rust 1.63 |
| Go | `go test ./bindings/go` | Go 1.21, a C compiler |
| Java (desktop) | `(cd bindings/java && ./gradlew build)` | JDK 17 or later, `cmake` on the path |
| Flutter | `cmake --build build`, then `(cd bindings/flutter && SPEECHWARP_LIBRARY=$PWD/../../build/libspeechwarp.dylib flutter test)` and `(cd bindings/flutter/example && flutter test integration_test -d macos)` | Flutter 3.24 |
| React Native | `(cd bindings/react-native && yarn install && yarn prepare && yarn typecheck && yarn test)` | Node 22 |

CI runs all of these on every push; see `.github/workflows/`.

## Rules of the house

**Do not edit `third_party/`.** Those files are copies of upstream at the commits in `third_party/README.md`.
A fix goes upstream, or into `src/` as a wrapper. To take a newer upstream, copy the same files again and
update the commit hashes; `tests/test_parity.c` will say whether behaviour changed, and `ctest`'s
`symbols_static` will say whether a new upstream symbol needs adding to `src/rename.h`.

**The C API is the contract.** Bindings expose what the header has and no more. A new capability starts as a
function in `include/speechwarp.h` with a test in `tests/test_stream.c`, and is then added to each binding
and to `docs/api.md`.

**Only `speechwarp_*` is visible.** The `symbols_static` and `symbols_shared` tests enforce it.

**Any build must work from five files.** Compiling `src/*.c` with `include/` and `third_party/kissfft/` on the
include path and `NDEBUG` defined is the whole build. The Swift, Python, Android and WebAssembly builds
depend on that, so a new source file has to be added to each of them: `CMakeLists.txt`, `Package.swift`,
`setup.py`, `bindings/android/speechwarp/src/main/cpp/CMakeLists.txt`, `bindings/js/build.sh`,
`bindings/rust/build.rs`, a `c_*.c` file in `bindings/go`, `bindings/flutter/src/CMakeLists.txt` with a file
in each of `bindings/flutter/ios/Classes` and `macos/Classes`, and `bindings/react-native/android/CMakeLists.txt`.

**Examples must run.** `examples/c/player.c` is a test. Run the others before changing them.

## Versions and releases

The version is `SPEECHWARP_VERSION` in `include/speechwarp.h`. CMake, Python and Gradle read it from there;
the other packages, the README's install lines and some doc comments repeat it. Do not edit them by hand:

```sh
tools/bump-version.sh 0.3.7      # every copy, the header's numeric macros, and a heading in both changelogs
```

Then write the changelog entries (the Flutter one is shown on pub.dev), run the tests, commit, and push
`main`. Pushing a tag `v0.3.7` on that commit publishes every package from GitHub Actions: PyPI, npm (the
JavaScript and React Native packages), NuGet, crates.io, pub.dev and Maven Central, with Go and Swift taking the
tag itself. Each release workflow first checks that the tag matches the version it is about to publish, and
refuses if not. A tag is never moved or reused, because the Go module proxy remembers every version it has seen.

## Licences

The project is Apache-2.0 and contains Apache-2.0 code from Google and Bill Cox and BSD-3-Clause code from
Mark Borgerding. Every package that ships the compiled library must ship `LICENSE`, `NOTICE` and the
third-party licence files; the .NET, Python and npm packages do, and their build files show how. Do not
describe the project in a way that suggests Google endorses it.
