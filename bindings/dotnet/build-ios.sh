#!/bin/sh
# Builds speechwarp.xcframework, holding the library for iPhones and for the simulator on both kinds of Mac,
# and puts it where the package looks for it: Speechwarp/native/ios/. Needs Xcode.
#
# It is a framework with a dynamic library inside because that is what an iOS app may embed; iOS does not
# allow a loose .dylib. Both slices are needed: a package with only the device one fails to link in
# simulator builds.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
minimum=15.0

build_slice() { # name, sdk, architectures
  cmake -S "$root" -B "$root/build-ios-$1" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$2" "-DCMAKE_OSX_ARCHITECTURES=$3" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=$minimum -DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    -DSPEECHWARP_FRAMEWORK=ON -DSPEECHWARP_BUILD_STATIC=OFF -DSPEECHWARP_BUILD_TESTS=OFF \
    -DSPEECHWARP_BUILD_TOOLS=OFF -DSPEECHWARP_INSTALL=OFF
  cmake --build "$root/build-ios-$1" --config Release -- -sdk "$2" -quiet
}

build_slice device iphoneos arm64
build_slice simulator iphonesimulator "arm64;x86_64"

out="$here/Speechwarp/native/ios/speechwarp.xcframework"
rm -rf "$out"
mkdir -p "$(dirname "$out")"
xcodebuild -create-xcframework \
  -framework "$root/build-ios-device/Release-iphoneos/speechwarp.framework" \
  -framework "$root/build-ios-simulator/Release-iphonesimulator/speechwarp.framework" \
  -output "$out"
