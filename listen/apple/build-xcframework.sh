#!/bin/sh
# Builds speechwarp_listen.xcframework: the speech-to-text library (with whisper.cpp inside) as a dynamic
# framework for iPhones, for the simulator on both kinds of Mac and, with "all", for macOS. Needs Xcode, CMake and
# the whisper.cpp submodule (git submodule update --init --checkout third_party/whisper.cpp).
#
#   listen/apple/build-xcframework.sh OUTPUT.xcframework          iOS devices and simulators (the .NET package)
#   listen/apple/build-xcframework.sh OUTPUT.xcframework all      and macOS too (the Swift package)
#
# Each framework carries the C header and a module map (module speechwarp_listen), so Swift can import it. It is a
# framework with a dynamic library inside because that is what an iOS app may embed. The GPU (Metal) is built in
# and used only when the app asks for it.
set -eu

out=${1:?usage: build-xcframework.sh OUTPUT.xcframework [ios|all]}
platforms=${2:-ios}
case "$platforms" in ios | all) ;; *) echo "build-xcframework.sh: platforms must be ios or all" >&2; exit 1 ;; esac

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
# Inside listen/, where the Swift package does not look (it would take the files whisper.cpp generates for its own).
work="$root/listen/build-apple"
# whisper.cpp calls Accelerate's newer interface, which these systems introduced; its own Apple builds use the same.
ios_minimum=16.4
macos_minimum=13.3
name=speechwarp_listen
version=$(sed -n 's/^#define SPEECHWARP_VERSION "\(.*\)"$/\1/p' "$root/include/speechwarp.h")
x86="-DGGML_SSE42=ON -DGGML_AVX=ON -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON"

if [ ! -f "$root/third_party/whisper.cpp/CMakeLists.txt" ]; then
  echo "build-xcframework.sh: whisper.cpp is missing; fetch it with" >&2
  echo "  git submodule update --init --checkout third_party/whisper.cpp" >&2
  exit 1
fi

generator=
command -v ninja >/dev/null 2>&1 && generator="-G Ninja"

# One architecture of one platform: slice name, SDK, architecture, then extra CMake arguments.
build_arch() {
  slice=$1 sdk=$2 arch=$3
  shift 3
  build="$work/$slice-$arch"
  # shellcheck disable=SC2086
  cmake -S "$root/listen" -B "$build" $generator -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_SYSROOT="$sdk" -DCMAKE_OSX_ARCHITECTURES="$arch" -DGGML_NATIVE=OFF -DSPEECHWARP_LISTEN_METAL=ON \
    -DSPEECHWARP_LISTEN_BUILD_STATIC=OFF -DSPEECHWARP_LISTEN_BUILD_TESTS=OFF -DSPEECHWARP_LISTEN_BUILD_TOOLS=OFF \
    -DSPEECHWARP_LISTEN_INSTALL=OFF -DCMAKE_SHARED_LINKER_FLAGS=-Wl,-headerpad_max_install_names "$@"
  cmake --build "$build" --config Release
}

# The framework's Info.plist: slice platform name, then any extra keys as XML.
info_plist() {
  cat <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleExecutable</key><string>$name</string>
  <key>CFBundleIdentifier</key><string>io.github.fredgaffey.speechwarp-listen</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>$name</string>
  <key>CFBundlePackageType</key><string>FMWK</string>
  <key>CFBundleShortVersionString</key><string>$version</string>
  <key>CFBundleVersion</key><string>$version</string>
  <key>CFBundleSupportedPlatforms</key><array><string>$1</string></array>
  $2
</dict>
</plist>
EOF
}

# Puts the header and the module map in a framework's folder for them.
add_interface() {
  mkdir -p "$1/Headers" "$1/Modules"
  cp "$root/listen/include/$name.h" "$1/Headers/"
  printf 'framework module %s {\n    umbrella header "%s.h"\n    export *\n}\n' "$name" "$name" > "$1/Modules/module.modulemap"
}

# An iOS framework (flat layout): slice name, platform name, the libraries to merge.
ios_framework() {
  slice=$1 platform=$2
  shift 2
  framework="$work/frameworks/$slice/$name.framework"
  rm -rf "$framework"
  mkdir -p "$framework"
  lipo -create "$@" -output "$framework/$name"
  install_name_tool -id "@rpath/$name.framework/$name" "$framework/$name"
  info_plist "$platform" "<key>MinimumOSVersion</key><string>$ios_minimum</string>" > "$framework/Info.plist"
  add_interface "$framework"
  frameworks="$frameworks -framework $framework"
}

# A macOS framework (versioned layout), from the libraries to merge.
macos_framework() {
  framework="$work/frameworks/macos/$name.framework"
  rm -rf "$framework"
  mkdir -p "$framework/Versions/A/Resources"
  lipo -create "$@" -output "$framework/Versions/A/$name"
  install_name_tool -id "@rpath/$name.framework/Versions/A/$name" "$framework/Versions/A/$name"
  info_plist MacOSX "<key>LSMinimumSystemVersion</key><string>$macos_minimum</string>" \
    > "$framework/Versions/A/Resources/Info.plist"
  add_interface "$framework/Versions/A"
  ln -s A "$framework/Versions/Current"
  for item in "$name" Headers Modules Resources; do
    ln -s "Versions/Current/$item" "$framework/$item"
  done
  frameworks="$frameworks -framework $framework"
}

library=lib$name.dylib
ios="-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=$ios_minimum"
frameworks=
# shellcheck disable=SC2086
{
  build_arch ios iphoneos arm64 $ios
  ios_framework ios iPhoneOS "$work/ios-arm64/$library"
  build_arch simulator iphonesimulator arm64 $ios
  build_arch simulator iphonesimulator x86_64 $ios $x86
  ios_framework simulator iPhoneSimulator "$work/simulator-arm64/$library" "$work/simulator-x86_64/$library"
  if [ "$platforms" = all ]; then
    build_arch macos macosx arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_minimum
    build_arch macos macosx x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$macos_minimum $x86
    macos_framework "$work/macos-arm64/$library" "$work/macos-x86_64/$library"
  fi
}

rm -rf "$out"
mkdir -p "$(dirname "$out")"
# shellcheck disable=SC2086
xcodebuild -create-xcframework $frameworks -output "$out"
echo "$out"
