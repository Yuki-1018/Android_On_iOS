import Foundation

struct AndroidProfile: Codable, Identifiable, Equatable, Sendable {
    let id: UUID
    var name: String
    let legacy: Bool
    static var supportDirectory: URL {
        FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
    }
    var directory: URL {
        directory(in: Self.supportDirectory)
    }
    func directory(in root: URL) -> URL {
        if legacy { return root.appendingPathComponent("Android51", isDirectory: true) }
        return root.appendingPathComponent("Profiles", isDirectory: true)
            .appendingPathComponent(id.uuidString, isDirectory: true)
    }
}

struct ImageCatalog: Decodable, Sendable {
    struct Entry: Decodable, Identifiable, Sendable {
        let name: String
        let url: URL
        var id: String { url.absoluteString }
    }
    let images: [Entry]
    static func decode(_ data: Data) throws -> [Entry] {
        guard data.count <= 1 << 20 else { throw EmuError.image("イメージ一覧が大きすぎます。") }
        let catalog = try JSONDecoder().decode(Self.self, from: data)
        guard catalog.images.count <= 200 else { throw EmuError.image("一覧の件数が多すぎます。") }
        var seen = Set<String>()
        return try catalog.images.map { entry in
            guard entry.url.scheme?.lowercased() == "https", entry.url.host != nil,
                  entry.url.user == nil, entry.url.password == nil,
                  !entry.name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
                  entry.name.count <= 120, seen.insert(entry.id).inserted else {
                throw EmuError.image("イメージ一覧の名前・URLが不正です。HTTPSの一意なURLが必要です。")
            }
            return entry
        }
    }
}
