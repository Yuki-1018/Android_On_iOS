import Foundation

struct VMConfiguration: Codable, Equatable, Sendable {
    enum RAM: Int, Codable, CaseIterable { case low = 512, balanced = 640, high = 768, maximum = 1024 }
    enum Cache: Int, Codable, CaseIterable { case small = 128, balanced = 192, performance = 256 }
    enum Resolution: String, Codable, CaseIterable {
        case performance = "360×640", low = "480×854", balanced = "540×960", high = "720×1280"
        var width: Int { switch self { case .performance: 360; case .low: 480; case .balanced: 540; case .high: 720 } }
        var height: Int { switch self { case .performance: 640; case .low: 854; case .balanced: 960; case .high: 1280 } }
    }
    var ram: RAM = .balanced
    var cache: Cache = .balanced
    var resolution: Resolution = .performance
    var cpuCount = 1
    var performanceOverlay = false

    func validate() throws {
        guard cpuCount == 1 else { throw EmuError.invalidConfiguration }
    }
}

enum EmuError: LocalizedError {
    case invalidConfiguration, image(String), jit(String), storage(String)
    var errorDescription: String? {
        switch self {
        case .invalidConfiguration: "このGoldfishボードは1 vCPU専用です。"
        case .image(let reason), .jit(let reason), .storage(let reason): reason
        }
    }
}
