"""Builds the Python extension from the C sources. Everything else is in pyproject.toml."""
import re
from pathlib import Path

from setuptools import Extension, setup

# The version lives in the public header, as it does for every other build of the library.
header = Path(__file__).parent.joinpath("include/speechwarp.h").read_text()
version = re.search(r'#define SPEECHWARP_VERSION "(.*)"', header).group(1)

setup(
    version=version,
    ext_modules=[
        Extension(
            "speechwarp._native",
            sources=[
                "bindings/python/speechwarp/_native.c",
                "src/speechwarp.c",
                "src/fft.c",
                "src/third_party_kissfft.c",
                "src/third_party_sonic.c",
                "src/third_party_speedy.c",
            ],
            include_dirs=["include", "third_party/kissfft"],
            # Upstream is full of assertions.
            define_macros=[("NDEBUG", None)],
        )
    ],
)
