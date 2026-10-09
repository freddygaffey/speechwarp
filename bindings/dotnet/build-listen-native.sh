#!/bin/sh
# Builds speechwarp_listen, the speech-to-text library (listen/, with whisper.cpp inside), and puts it where the
# Speechwarp.Listen package and its tests look for it: Speechwarp.Listen/runtimes/<runtime identifier>/native/.
#
#   ./build-listen-native.sh                  for this machine
#   ./build-listen-native.sh osx-x64          either macOS architecture, from either kind of Mac
#   ./build-listen-native.sh android-arm64    also android-x64 and android-arm; needs ANDROID_NDK_HOME
#
# It needs the whisper.cpp submodule, which a clone does not fetch by default:
#   git submodule update --init --checkout third_party/whisper.cpp
#
# The library is built for any machine of its kind, not tuned to this one: x86-64 needs AVX2, FMA and F16C (Intel
# Haswell and AMD Excavator, 2013, or later), arm64 the base instruction set. On Apple systems it can also use the
# GPU through Metal, when the app asks. macOS 13.3 or later is needed, because whisper.cpp calls Accelerate's newer
# interface (whisper.cpp's own Apple builds set the same minimum). Windows is built by CI with the same options
# (.github/workflows/release-listen.yml).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

if [ $# -gt 0 ]; then
  rid=$1
else
  case "$(uname -s)" in
    Darwin) os=osx ;;
    Linux) os=linux ;;
    *) echo "build-listen-native.sh: unsupported system $(uname -s)" >&2; exit 1 ;;
  esac
  case "$(uname -m)" in
    arm64 | aarch64) arch=arm64 ;;
    x86_64 | amd64) arch=x64 ;;
    *) echo "build-listen-native.sh: unsupported processor $(uname -m)" >&2; exit 1 ;;
  esac
  rid=$os-$arch
fi

if [ ! -f "$root/third_party/whisper.cpp/CMakeLists.txt" ]; then
  echo "build-listen-native.sh: whisper.cpp is missing; fetch it with" >&2
  echo "  git submodule update --init --checkout third_party/whisper.cpp" >&2
  exit 1
fi

# Not tuned to the machine that builds it (ggml would otherwise use -march=native).
x86="-DGGML_SSE42=ON -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON"
library=libspeechwarp_listen.so
case "$rid" in
  osx-arm64)
    library=libspeechwarp_listen.dylib
    set -- -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 -DSPEECHWARP_LISTEN_METAL=ON ;;
  osx-x64)
    library=libspeechwarp_listen.dylib
    # shellcheck disable=SC2086
    set -- -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3 -DSPEECHWARP_LISTEN_METAL=ON $x86 ;;
  linux-x64)
    # The C++ runtime goes inside, so the library runs on any glibc system without a matching libstdc++.
    # shellcheck disable=SC2086
    set -- "-DCMAKE_SHARED_LINKER_FLAGS=-static-libstdc++ -static-libgcc" $x86 ;;
  linux-arm64)
    set -- "-DCMAKE_SHARED_LINKER_FLAGS=-static-libstdc++ -static-libgcc" ;;
  android-arm64) abi=arm64-v8a ;;
  android-x64) abi=x86_64 ;;
  android-arm) abi=armeabi-v7a ;;
  *) echo "build-listen-native.sh: unknown runtime identifier $rid" >&2; exit 1 ;;
esac
case "$rid" in
  android-*)
    : "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to an Android NDK}"
    extra=
    # The x86-64 Android ABI promises SSE4.2 and no more.
    [ "$rid" = android-x64 ] && extra=-DGGML_SSE42=ON
    # 16 KB pages are required by Android 15 devices; NDK 27 and older need to be told. The C++ runtime is
    # linked in (the NDK's default, c++_static), so the app needs no libc++_shared.so.
    # shellcheck disable=SC2086
    set -- "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
      "-DANDROID_ABI=$abi" -DANDROID_PLATFORM=android-21 -DANDROID_STL=c++_static \
      "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384 -s" $extra
    ;;
esac

# Inside listen/, where the Swift package does not look (it would take the files whisper.cpp generates for its own).
build="$root/listen/build-dotnet-$rid"
generator=
command -v ninja >/dev/null 2>&1 && generator="-G Ninja"
# shellcheck disable=SC2086
cmake -S "$root/listen" -B "$build" $generator -DCMAKE_BUILD_TYPE=Release -DGGML_NATIVE=OFF \
  -DSPEECHWARP_LISTEN_BUILD_STATIC=OFF -DSPEECHWARP_LISTEN_BUILD_TESTS=OFF -DSPEECHWARP_LISTEN_BUILD_TOOLS=OFF \
  -DSPEECHWARP_LISTEN_INSTALL=OFF "$@"
cmake --build "$build" --config Release

out="$here/Speechwarp.Listen/runtimes/$rid/native"
mkdir -p "$out"
# -L: the build leaves a versioned file and symlinks to it; the package wants one plain file.
cp -L "$build/$library" "$out/$library"
echo "$out/$library"
