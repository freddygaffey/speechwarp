#!/bin/sh
# Sets the version everywhere it is written, ready for a release:
#
#   tools/bump-version.sh 0.3.7
#   (check the CHANGELOG entries, run the tests)
#   git commit -am "Release 0.3.7" && git push origin main
#   git tag v0.3.7 && git push origin v0.3.7      # publishes every package (see the release workflows)
#
# The version lives in include/speechwarp.h; every package, the install lines in the README and the examples
# repeat it, and each release workflow refuses to publish when the tag and the package disagree.
set -eu

new=${1:?usage: tools/bump-version.sh MAJOR.MINOR.PATCH}
echo "$new" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "not a version: $new" >&2; exit 1; }

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
old=$(sed -n 's/^#define SPEECHWARP_VERSION "\(.*\)"$/\1/p' include/speechwarp.h)
[ "$old" != "$new" ] || { echo "already $new" >&2; exit 1; }
if git tag -l "v$new" | grep -q .; then
  echo "tag v$new already exists: choose another version (tags are never reused)" >&2
  exit 1
fi

# Every tracked file that mentions the old version, except history (changelogs), lock files that pin other
# packages, vendored releases of tools, and the workflows (which only show it in examples).
escaped=$(echo "$old" | sed 's/\./\\./g')
git grep -l -F "$old" -- . ':!*CHANGELOG.md' ':!docs/*' ':!.github/*' ':!*.lock' ':!*/.yarn/*' \
  ':!*/package-lock.json' | while read -r file; do
  sed -i.bak "s/$escaped/$new/g" "$file" && rm "$file.bak"
  echo "  $file"
done

# The header's numeric parts, and the package's own entries in the npm lock file.
major=${new%%.*}; rest=${new#*.}; minor=${rest%%.*}; patch=${rest#*.}
sed -i.bak -e "s/^#define SPEECHWARP_VERSION_MAJOR .*/#define SPEECHWARP_VERSION_MAJOR $major/" \
  -e "s/^#define SPEECHWARP_VERSION_MINOR .*/#define SPEECHWARP_VERSION_MINOR $minor/" \
  -e "s/^#define SPEECHWARP_VERSION_PATCH .*/#define SPEECHWARP_VERSION_PATCH $patch/" include/speechwarp.h
rm include/speechwarp.h.bak
node -e '
  const fs = require("fs"), file = "bindings/js/package-lock.json", lock = JSON.parse(fs.readFileSync(file));
  lock.version = process.argv[1];
  lock.packages[""].version = process.argv[1];
  fs.writeFileSync(file, JSON.stringify(lock, null, 2) + "\n");
' "$new"

# Changelogs: an "Unreleased" section becomes this release; otherwise a heading is added to fill in. pub.dev
# shows the Flutter one, so it must have an entry for every version.
for log in CHANGELOG.md bindings/flutter/CHANGELOG.md; do
  if grep -q '^## Unreleased' "$log"; then
    sed -i.bak "s/^## Unreleased.*/## $new/" "$log"
  elif [ "$log" = CHANGELOG.md ]; then
    sed -i.bak "s/^# Changelog$/# Changelog\n\n## $new\n\n- (describe the changes)/" "$log"
  else
    { printf '## %s\n\n- (describe the changes)\n\n' "$new"; cat "$log"; } > "$log.bak" && cp "$log.bak" "$log"
  fi
  rm -f "$log.bak"
done

left=$(git grep -n -F "$old" -- . ':!*CHANGELOG.md' ':!docs/*' ':!.github/*' ':!*.lock' ':!*/.yarn/*' || true)
[ -z "$left" ] || { echo "still mentions $old:"; echo "$left"; }
echo "Version $old -> $new. Check the changelogs, run the tests, then commit and tag v$new."
