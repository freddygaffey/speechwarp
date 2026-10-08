#!/bin/sh
# Copies the C library's sources into cpp/speechwarp/, where the Android and iOS builds compile them. An npm
# package holds only its own folder, so it cannot reach the sources at the top of the repository the way the
# other bindings do. Runs automatically before packing and on `yarn install` in this folder; the copy is not
# committed.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
root=$(cd "$here/../.." && pwd)
out="$here/cpp/speechwarp"

# Installed from npm: the sources are already in the package and there is no repository to copy from.
[ -f "$root/include/speechwarp.h" ] || exit 0

rm -rf "$out"
mkdir -p "$out/include" "$out/src" "$out/licenses"
cp "$root/include/speechwarp.h" "$out/include/"
cp "$root"/src/*.c "$root"/src/*.h "$out/src/"
cp -R "$root/third_party" "$out/third_party"
cp "$root/LICENSE" "$root/NOTICE" "$out/licenses/"
# The package's own licence files, which npm and licence scanners look for at the top of the package.
cp "$root/LICENSE" "$root/NOTICE" "$here/"
