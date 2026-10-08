## Unreleased

Web support. The same classes now work in Flutter web, on top of the library compiled to WebAssembly, which is
inside the package.

- New `Speechwarp.initialize()` (and `Speechwarp.isInitialized`). On the web, await it once before using
  anything else; on every other platform it completes at once, so existing apps need no changes, though calling
  it is recommended so that the same code runs everywhere.
- The implementation moved to `lib/src/` (`ffi.dart` for native, `web.dart` for the web, chosen by a conditional
  export in `package:speechwarp/speechwarp.dart`). The public API is unchanged.
- pub.dev now lists the package as supporting Web.

## 0.3.5

No changes; published from GitHub.

## 0.3.4

The first release on pub.dev. The C sources now travel inside the package.

## 0.3.0

Rules that follow the speed, a syllable counter on its own, and the listener trainer and blind trials, all
off unless asked for. The full list is in the
[repository's changelog](https://github.com/fredgaffey/speechwarp/blob/main/CHANGELOG.md).
