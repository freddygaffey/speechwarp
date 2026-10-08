// swift-tools-version:5.7
// An example that uses the Speechwarp package from this repository. In your own project the dependency is
// .package(url: "https://github.com/fredgaffey/speechwarp", from: "0.3.0").
import PackageDescription

let package = Package(
    name: "speedup",
    platforms: [.macOS(.v10_15)],
    dependencies: [.package(name: "Speechwarp", path: "../..")],
    targets: [
        .executableTarget(
            name: "speedup",
            dependencies: [.product(name: "Speechwarp", package: "Speechwarp")]
        ),
    ]
)
