"""Copies into vendor/ the sources a source distribution of speechwarp-listen needs, which live outside this folder:
listen/, include/speechwarp.h, the licences, and the parts of whisper.cpp it builds (not its other GPU back ends,
examples or models). Run it before `python -m build --sdist`; a build from a clone does not need it.

    python bindings/python-listen/vendor.py
"""
import shutil
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
VENDOR = HERE / "vendor"
WHISPER = ROOT / "third_party" / "whisper.cpp"

# whisper.cpp's ggml back ends: only these are built (CPU, Apple's BLAS, Metal).
GGML_BACKENDS = {"ggml-cpu", "ggml-blas", "ggml-metal"}


def copy(source: Path, target: Path, ignore=None):
    if source.is_dir():
        shutil.copytree(source, target, ignore=ignore)
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def ggml_sources(directory, names):
    """The ggml back ends that are not built."""
    if Path(directory).name != "src" or Path(directory).parent.name != "ggml":
        return []
    return [name for name in names if name.startswith("ggml-") and (Path(directory) / name).is_dir()
            and name not in GGML_BACKENDS]


def main():
    if not (WHISPER / "CMakeLists.txt").exists():
        raise SystemExit("whisper.cpp is missing; fetch it with\n"
                         "  git submodule update --init --checkout third_party/whisper.cpp")
    shutil.rmtree(VENDOR, ignore_errors=True)
    copy(ROOT / "LICENSE", VENDOR / "LICENSE")
    copy(ROOT / "NOTICE", VENDOR / "NOTICE")
    copy(ROOT / "include" / "speechwarp.h", VENDOR / "include" / "speechwarp.h")
    copy(ROOT / "listen", VENDOR / "listen", ignore=shutil.ignore_patterns("build*", "tests", "tools", "apple"))
    target = VENDOR / "third_party" / "whisper.cpp"
    for name in ("CMakeLists.txt", "LICENSE", "cmake", "include", "src"):
        copy(WHISPER / name, target / name)
    for path in WHISPER.glob("*.in"):
        copy(path, target / path.name)
    ggml = WHISPER / "ggml"
    for name in ("CMakeLists.txt", "LICENSE", "cmake", "include"):
        if (ggml / name).exists():
            copy(ggml / name, target / "ggml" / name)
    copy(ggml / "src", target / "ggml" / "src", ignore=ggml_sources)
    print(f"vendored into {VENDOR}")


if __name__ == "__main__":
    main()
