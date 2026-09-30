#!/bin/sh
# Builds the native library and puts it where the package and the tests look for it:
# Speechwarp/runtimes/<runtime identifier>/native/.
#
#   ./build-native.sh              for this machine
#   ./build-native.sh osx-x64      either macOS architecture, from either kind of Mac
#   ./build-native.sh android-arm64   also android-x64 and android-arm; needs ANDROID_NDK_HOME
#
# Windows is built by CI with the same CMake options (.github/workflows/dotnet.yml).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

if [ $# -gt 0 ]; then
  rid=$1
else
  case "$(uname -s)" in
    Darwin) os=osx ;;
    Linux) os=linux ;;
    *) echo "build-native.sh: unsupported system $(uname -s)" >&2; exit 1 ;;
  esac
  case "$(uname -m)" in
    arm64 | aarch64) arch=arm64 ;;
    x86_64 | amd64) arch=x64 ;;
    *) echo "build-native.sh: unsupported processor $(uname -m)" >&2; exit 1 ;;
  esac
  rid=$os-$arch
fi

library=libspeechwarp.so
case "$rid" in
  osx-arm64) library=libspeechwarp.dylib; set -- -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 ;;
  osx-x64) library=libspeechwarp.dylib; set -- -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15 ;;
  linux-x64 | linux-arm64) set -- ;;
  android-arm64) abi=arm64-v8a ;;
  android-x64) abi=x86_64 ;;
  android-arm) abi=armeabi-v7a ;;
  *) echo "build-native.sh: unknown runtime identifier $rid" >&2; exit 1 ;;
esac
case "$rid" in
  android-*)
    : "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to an Android NDK}"
    # 16 KB pages are required by Android 15 devices; NDK 27 and older need to be told.
    set -- "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
      "-DANDROID_ABI=$abi" -DANDROID_PLATFORM=android-21 \
      "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384 -s"
    ;;
esac

build="$root/build-dotnet-$rid"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release \
  -DSPEECHWARP_BUILD_STATIC=OFF -DSPEECHWARP_BUILD_TESTS=OFF -DSPEECHWARP_BUILD_TOOLS=OFF "$@"
cmake --build "$build" --config Release

out="$here/Speechwarp/runtimes/$rid/native"
mkdir -p "$out"
# -L: the build leaves a versioned file and symlinks to it; the package wants one plain file.
cp -L "$build/$library" "$out/$library"
echo "$out/$library"
