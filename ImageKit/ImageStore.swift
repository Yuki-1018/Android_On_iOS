import Foundation
import CryptoKit

actor ImageStore {
    private let manager = FileManager.default
    private let root: URL
    init(root: URL? = nil) {
        self.root = root ?? FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("Android51", isDirectory: true)
    }
    func current() throws -> ImageManifest? {
        let manifest = root.appendingPathComponent("image.json")
        guard manager.fileExists(atPath: manifest.path) else { return nil }
        _ = try regularFile(manifest, maximum: 65536)
        let data = try Data(contentsOf: manifest)
        let result = try JSONDecoder().decode(ImageManifest.self, from: data)
        guard result.schema == 1, ImageProfile.api(for: result.profile) != nil else { throw EmuError.image("非対応の保存イメージです。") }
        for name in ["kernel", "ramdisk.img", "system.img", "userdata.img"] {
            guard let entry = result.files.first(where: { $0.name == name }), entry.bytes > 0,
                  try regularFile(root.appendingPathComponent(name), maximum: 8 << 30) == entry.bytes else {
                throw EmuError.image("保存イメージが不足または変更されています: \(name)")
            }
        }
        return result
    }
    private func regularFile(_ url: URL, maximum: Int64) throws -> Int64 {
        let info = try url.resourceValues(forKeys: [.isRegularFileKey, .isSymbolicLinkKey, .fileSizeKey])
        guard info.isRegularFile == true, info.isSymbolicLink != true,
              let size = info.fileSize, size > 0, Int64(size) <= maximum else {
            throw EmuError.image("通常ファイル以外、空ファイル、またはサイズ上限超過: \(url.lastPathComponent)")
        }
        return Int64(size)
    }
    func ensureCache() throws {
        guard try current() != nil else { throw EmuError.image("先にAndroidイメージを取り込んでください。") }
        let cache = root.appendingPathComponent("cache.img")
        // Preserve imported/existing cache contents, including across upgrades.
        if (try? cache.resourceValues(forKeys: [.isSymbolicLinkKey])) != nil {
            _ = try regularFile(cache, maximum: 8 << 30)
            return
        }
        guard let template = Bundle.main.url(forResource: "cache-template", withExtension: "sparse") else {
            throw EmuError.image("空のcacheテンプレートがありません。更新したIPAをインストールしてください。")
        }
        let staged = root.appendingPathComponent("cache-create-\(UUID().uuidString).tmp")
        defer { try? manager.removeItem(at: staged) }
        var error = [CChar](repeating: 0, count: 1024)
        let capacity = error.count
        let copied = template.path.withCString { source in
            staged.path.withCString { destination in
                AEImportImage(source, destination, 64 << 20, &error, capacity)
            }
        }
        guard copied else {
            throw EmuError.image(error.withUnsafeBufferPointer { String(cString: $0.baseAddress!) })
        }
        guard try regularFile(staged, maximum: 64 << 20) == (64 << 20) else {
            throw EmuError.image("cacheテンプレートの展開サイズが不正です。")
        }
        // The completed file is published only after the importer has fsynced.
        // moveItem refuses to replace an existing destination.
        try manager.moveItem(at: staged, to: cache)
    }
    private func readMetadata(_ url: URL) throws -> [String: String] {
        _ = try regularFile(url, maximum: 65536)
        return try ImageProfile.properties(String(contentsOf: url, encoding: .utf8))
    }
    private func hash(_ url: URL) throws -> String {
        let file = try FileHandle(forReadingFrom: url)
        defer { try? file.close() }
        var sha = SHA256()
        // FileHandle may return autoreleased NSData on Darwin. Actor jobs do
        // not drain a pool per iteration: large SDK images could retain GiBs.
        while try autoreleasepool(invoking: { () throws -> Bool in
            try Task.checkCancellation()
            guard let data = try file.read(upToCount: 1 << 20), !data.isEmpty else { return false }
            sha.update(data: data)
            return true
        }) {}
        return sha.finalize().map { String(format: "%02x", $0) }.joined()
    }
    func importDirectory(_ directory: URL) throws -> ImageManifest {
        let scoped = directory.startAccessingSecurityScopedResource()
        defer { if scoped { directory.stopAccessingSecurityScopedResource() } }
        let dirInfo = try directory.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
        guard dirInfo.isDirectory == true, dirInfo.isSymbolicLink != true else { throw EmuError.image("イメージフォルダを選択してください。") }
        var metadata = try readMetadata(directory.appendingPathComponent("source.properties"))
        let hardware = directory.appendingPathComponent("hardware-properties.ini")
        if manager.fileExists(atPath: hardware.path) {
            for (key, value) in try readMetadata(hardware) {
                if let old = metadata[key], old != value { throw EmuError.image("メタデータが矛盾しています: \(key)") }
                metadata[key] = value
            }
        }
        try ImageProfile.validate(metadata)
        let kernelName = manager.fileExists(atPath: directory.appendingPathComponent("kernel-qemu").path) ? "kernel-qemu" : "kernel"
        var names = [(kernelName, "kernel"), ("ramdisk.img", "ramdisk.img"), ("system.img", "system.img"), ("userdata.img", "userdata.img")]
        if manager.fileExists(atPath: directory.appendingPathComponent("cache.img").path) { names.append(("cache.img", "cache.img")) }
        // Check the ARM zImage signature instead of accepting an arbitrary named kernel.
        let kernelURL = directory.appendingPathComponent(kernelName)
        _ = try regularFile(kernelURL, maximum: 64 << 20)
        let kernel = try FileHandle(forReadingFrom: kernelURL)
        let prefix: Data
        do { prefix = try kernel.read(upToCount: 64) ?? Data(); try kernel.close() }
        catch { try? kernel.close(); throw error }
        guard prefix.count >= 40, Array(prefix[36..<40]) == [0x18, 0x28, 0x6f, 0x01] else { throw EmuError.image("ARM Goldfish用zImage kernelを確認できません。") }
        let parent = root.deletingLastPathComponent()
        try manager.createDirectory(at: parent, withIntermediateDirectories: true)
        var stage = parent.appendingPathComponent("Android51-import-\(UUID().uuidString)", isDirectory: true)
        try manager.createDirectory(at: stage, withIntermediateDirectories: false)
        defer { try? manager.removeItem(at: stage) }
        var excluded = URLResourceValues(); excluded.isExcludedFromBackup = true
        try stage.setResourceValues(excluded)
        var records: [ImageManifest.File] = []
        var total: Int64 = 0
        for (sourceName, targetName) in names {
            try Task.checkCancellation()
            let src = directory.appendingPathComponent(sourceName), dst = stage.appendingPathComponent(targetName)
            let perFileLimit: Int64 = targetName == "kernel" || targetName == "ramdisk.img" ? 64 << 20 : 8 << 30
            let limit = min(perFileLimit, (16 << 30) - total)
            _ = try regularFile(src, maximum: limit)
            var error = [CChar](repeating: 0, count: 1024)
            let errorCapacity = error.count
            let copied = src.path.withCString { s in dst.path.withCString { d in AEImportImage(s, d, UInt64(limit), &error, errorCapacity) } }
            guard copied else {
                let message = error.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
                throw EmuError.image("\(sourceName): \(message)")
            }
            if ["system.img", "userdata.img", "cache.img"].contains(targetName) {
                let disk = try FileHandle(forReadingFrom: dst)
                defer { try? disk.close() }
                try disk.seek(toOffset: 1080)
                guard try disk.read(upToCount: 2) == Data([0x53, 0xef]) else {
                    throw EmuError.image("\(targetName)はext4ではありません。YAFFS2/F2FS形式の古いイメージは現在非対応です。")
                }
            }
            let size = try regularFile(dst, maximum: limit)
            total += size
            guard total <= 16 << 30 else { throw EmuError.image("展開後の合計サイズが16 GiBを超えています。") }
            records.append(.init(name: targetName, bytes: size, sha256: try hash(dst)))
            if targetName == "system.img" || targetName == "kernel" || targetName == "ramdisk.img" {
                try manager.setAttributes([.posixPermissions: 0o400], ofItemAtPath: dst.path)
            }
        }
        let manifest = ImageManifest(schema: 1, profile: ImageProfile.identifier(api: Int(metadata["AndroidVersion.ApiLevel"]!)!), importedAt: Date(), files: records)
        try JSONEncoder().encode(manifest).write(to: stage.appendingPathComponent("image.json"), options: .atomic)
        try Task.checkCancellation()
        if manager.fileExists(atPath: root.path) { _ = try manager.replaceItemAt(root, withItemAt: stage) }
        else { try manager.moveItem(at: stage, to: root) }
        var committed = root; try committed.setResourceValues(excluded)
        return manifest
    }
}
