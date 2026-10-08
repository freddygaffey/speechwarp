#!/bin/sh
# Copies the C library's sources into src/speechwarp/, laid out as they are at the top of the repository, and
# the licence and notice files into the package folder. A pub.dev package holds only its own folder, so it
# cannot reach the sources at the top of the repository the way a git dependency can. Run by the release
# workflow before `flutter pub publish`; the copies are not committed (see .gitignore and .pubignore).
#
# When the copy is there, every build uses it; when it is not, the builds fall back to the repository's own
# sources. So after changing the C sources, run this again or delete src/speechwarp/.
#
# It also runs scripts/build_web.sh, which compiles the library to WebAssembly into lib/src/wasm_bytes.dart (the
# web support) and needs Emscripten. Without emcc this stops with an error rather than leave the web support
# out of the package; set SPEECHWARP_SKIP_WEB=1 to do the rest anyway, for work that is not going to publish.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
root=$(cd "$here/../.." && pwd)
out="$here/src/speechwarp"

[ -f "$root/include/speechwarp.h" ] || { echo "No repository to copy from at $root" >&2; exit 1; }

rm -rf "$out"
mkdir -p "$out/include" "$out/src"
cp "$root/include/speechwarp.h" "$out/include/"
cp "$root"/src/*.c "$root"/src/*.h "$out/src/"
# The upstream sources keep their own licences beside them.
cp -R "$root/third_party" "$out/third_party"
# pub.dev wants LICENSE at the top of the package.
cp "$root/LICENSE" "$root/NOTICE" "$here/"

if [ "${SPEECHWARP_SKIP_WEB:-}" = 1 ]; then
  echo "SPEECHWARP_SKIP_WEB is set: lib/src/wasm_bytes.dart left as it is; a package published now has no web support" >&2
else
  sh "$here/scripts/build_web.sh"
fi
