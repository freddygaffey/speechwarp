// swift-tools-version:5.7
// The Swift package. It is at the top of the repository because that is where Swift Package Manager looks,
// and because it compiles the C sources in src/ directly.
import PackageDescription

let package = Package(
    name: "Speechwarp",
    platforms: [.macOS(.v10_15), .iOS(.v13), .tvOS(.v13), .watchOS(.v6)],
    products: [
        .library(name: "Speechwarp", targets: ["Speechwarp"]),
    ],
    targets: [
        .target(
            name: "CSpeechwarp",
            path: ".",
            // Everything here that is not this target's. Without it Swift Package Manager takes the example
            // apps of the other bindings for resources of this one.
            exclude: [
                "bindings", "examples", "tests", "tools", "docs", "cmake", "third_party",
                "CMakeLists.txt", "Cargo.toml", "go.mod", "pyproject.toml", "setup.py", "MANIFEST.in",
                "README.md", "CHANGELOG.md", "CONTRIBUTING.md", "LICENSE", "NOTICE",
                "src/internal.h", "src/rename.h", "src/fft.h",
            ],
            sources: [
                "src/speechwarp.c",
                "src/fft.c",
                "src/trainer.c",
                "src/third_party_kissfft.c",
                "src/third_party_sonic.c",
                "src/third_party_speedy.c",
            ],
            publicHeadersPath: "include",
            cSettings: [
                .headerSearchPath("third_party/kissfft"),
                // Upstream is full of assertions.
                .define("NDEBUG"),
            ]
        ),
        .target(
            name: "Speechwarp",
            dependencies: ["CSpeechwarp"],
            path: "bindings/swift/Sources/Speechwarp"
        ),
        .testTarget(
            name: "SpeechwarpTests",
            dependencies: ["Speechwarp"],
            path: "bindings/swift/Tests/SpeechwarpTests"
        ),
    ]
)
