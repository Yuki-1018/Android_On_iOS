import Foundation

enum RemoteImages {
    static let catalogURL = URL(string: "https://api.yuki-0604.f5.si/images.json")!
    static func catalog() async throws -> [ImageCatalog.Entry] {
        let file = try await DownloadRetry.fetch(catalogURL, limit: 1 << 20, progress: { _, _ in })
        defer { try? FileManager.default.removeItem(at: file) }
        return try ImageCatalog.decode(Data(contentsOf: file))
    }
    static func install(_ entry: ImageCatalog.Entry, into profile: AndroidProfile,
                        progress: @escaping @Sendable (Int64, Int64) -> Void) async throws -> ImageManifest {
        let file = try await DownloadRetry.fetch(entry.url, limit: Int64(UInt32.max) - 1, progress: progress)
        let extracted = FileManager.default.temporaryDirectory.appendingPathComponent("AndroidEmu-zip-\(UUID().uuidString)")
        defer {
            try? FileManager.default.removeItem(at: file)
            try? FileManager.default.removeItem(at: extracted)
        }
        try Task.checkCancellation()
        progress(-1, -1)
        let extraction = Task.detached(priority: .userInitiated) {
            var error = [CChar](repeating: 0, count: 1024)
            let capacity = error.count
            let ok = file.path.withCString { source in
                extracted.path.withCString { target in AEExtractImageZip(source, target, &error, capacity, { Task<Never, Never>.isCancelled }) }
            }
            guard ok else { throw EmuError.image(error.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }) }
            let enumerator = FileManager.default.enumerator(at: extracted, includingPropertiesForKeys: [.isRegularFileKey])
            var directories: [URL] = []
            while let url = enumerator?.nextObject() as? URL {
                if url.lastPathComponent == "source.properties" { directories.append(url.deletingLastPathComponent()) }
            }
            guard directories.count == 1 else { throw EmuError.image("ZIPにはsource.propertiesを含むAndroidイメージ一式を1組だけ格納してください。") }
            return directories[0]
        }
        let directory = try await withTaskCancellationHandler(operation: {
            do { return try await extraction.value }
            catch { try Task.checkCancellation(); throw error }
        }, onCancel: { extraction.cancel() })
        try Task.checkCancellation()
        return try await ImageStore(root: profile.directory).importDirectory(directory)
    }
}
