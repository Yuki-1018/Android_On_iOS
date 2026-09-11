import Foundation

enum ImageProfile {
    static let identifier = "android-5.1.1-api22-armv7-goldfish"
    static func properties(_ text: String) throws -> [String: String] {
        var values: [String: String] = [:]
        for line in text.components(separatedBy: .newlines) {
            let line = line.trimmingCharacters(in: .whitespaces)
            if line.isEmpty || line.hasPrefix("#") || line.hasPrefix("!") { continue }
            guard let separator = line.firstIndex(of: "=") else { continue }
            let key = String(line[..<separator]).trimmingCharacters(in: .whitespaces)
            let value = String(line[line.index(after: separator)...]).trimmingCharacters(in: .whitespaces)
            if let old = values[key], old != value { throw EmuError.image("メタデータに矛盾があります: \(key)") }
            values[key] = value
        }
        return values
    }
    static let versions: [Int: String] = [14: "4.0", 15: "4.0", 16: "4.1", 17: "4.2", 18: "4.3", 19: "4.4", 21: "5.0", 22: "5.1", 23: "6.0"]
    static func identifier(api: Int) -> String {
        api == 22 ? identifier : "android-api\(api)-armv7-goldfish"
    }
    static func api(for identifier: String) -> Int? {
        versions.keys.first { self.identifier(api: $0) == identifier }
    }
    static func label(for identifier: String) -> String {
        guard let api = api(for: identifier), let version = versions[api] else { return "Android" }
        return "Android \(version) · API \(api)"
    }
    static func validate(_ properties: [String: String]) throws {
        guard let raw = properties["AndroidVersion.ApiLevel"], let api = Int(raw),
              let version = versions[api], properties["SystemImage.Abi"] == "armeabi-v7a" else {
            throw EmuError.image("Android 4〜6のARMv7 Goldfishイメージと元のsource.propertiesが必要です。")
        }
        let tag = properties["SystemImage.TagId"]
        guard tag == "default" || (tag == nil && api <= 18) else {
            throw EmuError.image("defaultのシステムイメージを選択してください。Google APIs・Wearは対象外です。")
        }
        for (key, expected) in [("hw.cpu.arch", "arm"), ("hw.board", "goldfish")] {
            if let value = properties[key], value != expected { throw EmuError.image("非対応イメージ: \(key)=\(value)") }
        }
        if let value = properties["Platform.Version"], value != version && !value.hasPrefix(version + ".") {
            throw EmuError.image("APIレベルとAndroidバージョンが一致しません。")
        }
    }

}

struct ImageManifest: Codable, Sendable {
    struct File: Codable, Sendable { let name: String; let bytes: Int64; let sha256: String }
    let schema: Int
    let profile: String
    let importedAt: Date
    let files: [File]
}
