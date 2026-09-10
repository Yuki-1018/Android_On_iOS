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
    static func validate(_ properties: [String: String]) throws {
        // Metadata is evidence of compatibility, not a cryptographic identity guarantee.
        for (key, expected) in [("AndroidVersion.ApiLevel", "22"), ("SystemImage.Abi", "armeabi-v7a"),
                                ("SystemImage.TagId", "default"), ("Platform.Version", "5.1.1"),
                                ("hw.cpu.arch", "arm"), ("hw.board", "goldfish")] {
            if let actual = properties[key], actual != expected {
                throw EmuError.image("非対応イメージ: \(key)=\(actual)。必要値は\(expected)です。")
            }
        }
        guard properties["AndroidVersion.ApiLevel"] == "22",
              properties["SystemImage.Abi"] == "armeabi-v7a",
              properties["SystemImage.TagId"] == "default" else {
            throw EmuError.image("source.propertiesでAPI 22 / armeabi-v7a / defaultを確認できません。SDKの元メタデータを含むフォルダを選択してください。")
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
