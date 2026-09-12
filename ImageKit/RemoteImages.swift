import Foundation

// URLSession writes to disk; no multi-GB ZIP is ever loaded into Data.
private final class BoundedDownload: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
    private let lock = NSLock()
    private var task: URLSessionDownloadTask?
    private var cancelled = false
    private var continuation: CheckedContinuation<URL, Error>?
    private var result: Result<URL, Error>?
    private var lastProgress = Date.distantPast
    private let limit: Int64
    private let progress: @Sendable (Int64, Int64) -> Void
    init(limit: Int64, progress: @escaping @Sendable (Int64, Int64) -> Void) {
        self.limit = limit; self.progress = progress
    }
    func run(_ url: URL) async throws -> URL {
        try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { continuation in
                lock.lock()
                if cancelled { lock.unlock(); continuation.resume(throwing: CancellationError()); return }
                self.continuation = continuation
                let config = URLSessionConfiguration.ephemeral
                config.timeoutIntervalForRequest = 60
                config.timeoutIntervalForResource = 7200
                let queue = OperationQueue(); queue.maxConcurrentOperationCount = 1
                let session = URLSession(configuration: config, delegate: self, delegateQueue: queue)
                let task = session.downloadTask(with: url)
                self.task = task
                lock.unlock()
                task.resume()
            }
        }, onCancel: {
            self.lock.lock(); self.cancelled = true; let task = self.task; self.lock.unlock()
            task?.cancel()
        })
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(request.url?.scheme?.lowercased() == "https" ? request : nil)
    }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didWriteData bytesWritten: Int64,
                    totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        if totalBytesWritten > limit || totalBytesExpectedToWrite > limit {
            result = .failure(EmuError.image("ダウンロードのサイズ上限を超えています。")); downloadTask.cancel()
        }
        let now = Date()
        if now.timeIntervalSince(lastProgress) >= 0.25 || totalBytesWritten == totalBytesExpectedToWrite {
            lastProgress = now
            progress(totalBytesWritten, totalBytesExpectedToWrite)
        }
    }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        do {
            guard let response = downloadTask.response as? HTTPURLResponse, (200...299).contains(response.statusCode),
                  response.url?.scheme?.lowercased() == "https" else { throw EmuError.image("サーバーからダウンロードできませんでした。時間をおいて再試行してください。") }
            let size = try location.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
            guard size > 0, Int64(size) <= limit else { throw EmuError.image("ダウンロードしたファイルのサイズが不正です。") }
            let saved = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
            try FileManager.default.moveItem(at: location, to: saved)
            result = .success(saved)
        } catch { result = .failure(error) }
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        lock.lock(); let continuation = self.continuation; self.continuation = nil; self.task = nil
        let cancelled = self.cancelled; lock.unlock()
        if cancelled || error != nil {
            if case .success(let url)? = result { try? FileManager.default.removeItem(at: url) }
            if cancelled { continuation?.resume(throwing: CancellationError()) }
            else if case .failure(let reason)? = result { continuation?.resume(throwing: reason) }
            else { continuation?.resume(throwing: error!) }
        } else { continuation?.resume(with: result ?? .failure(EmuError.image("ダウンロードに失敗しました。"))) }
        session.finishTasksAndInvalidate()
    }
}

enum RemoteImages {
    static let catalogURL = URL(string: "https://api.yuki-0604.f5.si/images.json")!
    static func catalog() async throws -> [ImageCatalog.Entry] {
        let file = try await BoundedDownload(limit: 1 << 20, progress: { _, _ in }).run(catalogURL)
        defer { try? FileManager.default.removeItem(at: file) }
        return try ImageCatalog.decode(Data(contentsOf: file))
    }
    static func install(_ entry: ImageCatalog.Entry, into profile: AndroidProfile,
                        progress: @escaping @Sendable (Int64, Int64) -> Void) async throws -> ImageManifest {
        let file = try await BoundedDownload(limit: Int64(UInt32.max) - 1, progress: progress).run(entry.url)
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
