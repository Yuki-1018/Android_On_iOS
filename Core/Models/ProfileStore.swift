import Foundation

actor ProfileStore {
    private let root: URL
    init(root: URL = AndroidProfile.supportDirectory) { self.root = root }
    private var index: URL { root.appendingPathComponent("profiles.json") }
    func load() throws -> [AndroidProfile] {
        if FileManager.default.fileExists(atPath: index.path) {
            let size = try index.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
            guard size > 0, size <= 65536 else { throw EmuError.storage("プロファイル一覧が不正です。") }
            let profiles = try JSONDecoder().decode([AndroidProfile].self, from: Data(contentsOf: index))
            guard profiles.count <= 100,
                  Set(profiles.map(\.id)).count == profiles.count,
                  profiles.filter(\.legacy).count <= 1 else { throw EmuError.storage("プロファイル一覧が不正です。") }
            return profiles
        }
        // Preserve the existing directory in place; no migration copies or
        // renames can lose a user's userdata/cache on an interrupted upgrade.
        let legacy = FileManager.default.fileExists(atPath: root.appendingPathComponent("Android51/image.json").path)
        let profiles = legacy ? [AndroidProfile(id: UUID(), name: "これまでのAndroid", legacy: true)] : []
        try save(profiles)
        return profiles
    }
    func save(_ profiles: [AndroidProfile]) throws {
        guard profiles.count <= 100, Set(profiles.map(\.id)).count == profiles.count,
              profiles.filter(\.legacy).count <= 1,
              profiles.allSatisfy({ !$0.name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && $0.name.count <= 120 }) else {
            throw EmuError.storage("プロファイル一覧が不正です。")
        }
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        try JSONEncoder().encode(profiles).write(to: index, options: .atomic)
    }
    func remove(_ profile: AndroidProfile, remaining: [AndroidProfile]) throws {
        guard !remaining.contains(where: { $0.id == profile.id }) else { throw EmuError.storage("削除対象が一覧に残っています。") }
        try save(remaining)
        // Publish the new index first. A failed removal leaves an unreferenced
        // directory, never an indexed profile pointing at deleted user data.
        if FileManager.default.fileExists(atPath: profile.directory(in: root).path) {
            try FileManager.default.removeItem(at: profile.directory(in: root))
        }
    }
}
