#!/bin/sh
# Builds speechwarp_listen.xcframework (iOS devices and simulators, and macOS) into Frameworks/, where Package.swift
# looks for it. The SpeechwarpListen product exists only once it has been built: Swift Package Manager cannot build
# whisper.cpp itself (see README.md). Needs Xcode, CMake and the whisper.cpp submodule:
#   git submodule update --init --checkout third_party/whisper.cpp
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
"$root/listen/apple/build-xcframework.sh" "$here/Frameworks/speechwarp_listen.xcframework" all
