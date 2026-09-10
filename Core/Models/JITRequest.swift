import Foundation

enum FeaturePresence: Int, Sendable {
    case unknown = -1, absent = 0, present = 1
    var label: String { switch self { case .unknown: "Unknown"; case .absent: "Not Present"; case .present: "Present" } }
}

enum JITRequest {
    static func url(bundleID: String, pid: Int32, txm: FeaturePresence, sptm: FeaturePresence) throws -> URL {
        guard !bundleID.isEmpty, pid > 0, txm != .unknown, sptm != .unknown else {
            throw EmuError.jit("TXM/SPTMまたはプロセス情報を確認できません。JITは開始していません。")
        }
        var parts = URLComponents()
        parts.scheme = "stikdebug"; parts.host = "enable-jit"
        parts.queryItems = [.init(name: "bundle-id", value: bundleID), .init(name: "pid", value: String(pid))]
        if txm == .present || sptm == .present { parts.queryItems?.append(.init(name: "script-name", value: "universal.js")) }
        guard let url = parts.url else { throw EmuError.jit("StikDebug URLを作成できません。") }
        return url
    }
}

