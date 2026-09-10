// swift-tools-version: 5.10
import PackageDescription

let package = Package(
    name: "AndroidEmuModels",
    platforms: [.macOS(.v13), .iOS("26.0")],
    products: [.library(name: "AndroidEmuModels", targets: ["AndroidEmuModels"])],
    targets: [
        .target(name: "AndroidEmuModels", path: "Core/Models"),
        .testTarget(name: "AndroidEmuModelTests", dependencies: ["AndroidEmuModels"], path: "Tests/Swift")
    ]
)
