// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "ArcaMac",
    platforms: [
        .macOS(.v13)
    ],
    products: [
        .executable(name: "ArcaMac", targets: ["ArcaMac"])
    ],
    targets: [
        .systemLibrary(
            name: "CArcaCore",
            path: "Sources/CArcaCore"
        ),
        .executableTarget(
            name: "ArcaMac",
            dependencies: ["CArcaCore"],
            path: "Sources/ArcaMac"
        )
    ]
)
