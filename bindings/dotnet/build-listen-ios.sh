#!/bin/sh
# Builds speechwarp_listen.xcframework, the speech-to-text library for iPhones and for the simulator on both kinds
# of Mac, and puts it where the Speechwarp.Listen package looks for it: Speechwarp.Listen/native/ios/. Needs Xcode
# and the whisper.cpp submodule (git submodule update --init --checkout third_party/whisper.cpp).
#
# As for the core package (build-ios.sh), it is a framework with a dynamic library inside, with both slices: a
# package with only the device one fails to link in simulator builds.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
"$root/listen/apple/build-xcframework.sh" "$here/Speechwarp.Listen/native/ios/speechwarp_listen.xcframework" ios
