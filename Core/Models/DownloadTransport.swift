import Foundation

// URLSession writes to disk; no multi-GB ZIP is ever loaded into Data.
final class BoundedDownload: NSObject, URLSessionDownloadDelegate, @unchecked Sendable {
    private let lock = NSLock()
    private var task: URLSessionDownloadTask?
    private var cancelled = false
    private var continuation: CheckedContinuation<URL, Error>?
    private var result: Result<URL, Error>?
    private var lastProgress = Date.distantPast
    private var lastSpaceCheck = Date.distantPast
    private let limit: Int64
    private let resumeData: Data?
    private let progress: @Sendable (Int64, Int64) -> Void
    init(limit: Int64, progress: @escaping @Sendable (Int64, Int64) -> Void, resumeData: Data? = nil) {
        self.limit = limit; self.progress = progress; self.resumeData = resumeData
    }
    func run(_ url: URL) async throws -> URL {
        try await withTaskCancellationHandler(operation: {
            try await withCheckedThrowingContinuation { continuation in
                lock.lock()
                if cancelled { lock.unlock(); continuation.resume(throwing: CancellationError()); return }
                self.continuation = continuation
                let config = URLSessionConfiguration.ephemeral
                // Catalog requests fail promptly so the import alternative
                // stays usable offline. Large transfers can await reconnection.
                let largeTransfer = limit > 1 << 20
                config.timeoutIntervalForRequest = largeTransfer ? 120 : 30
                config.timeoutIntervalForResource = largeTransfer ? 7200 : 120
                config.waitsForConnectivity = largeTransfer
                let queue = OperationQueue(); queue.maxConcurrentOperationCount = 1
                let session = URLSession(configuration: config, delegate: self, delegateQueue: queue)
                let task = self.resumeData.map { session.downloadTask(withResumeData: $0) } ?? session.downloadTask(with: url)
                self.task = task
                lock.unlock()
                task.resume()
            }
        }, onCancel: {
            self.lock.lock(); self.cancelled = true; let task = self.task; self.lock.unlock()
            task?.cancel()
        })
    }
    func urlSession(_ session: URLSession, taskIsWaitingForConnectivity task: URLSessionTask) {
        progress(-2, -1)
    }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didResumeAtOffset fileOffset: Int64, expectedTotalBytes: Int64) {
        if fileOffset > limit || expectedTotalBytes > limit {
            result = .failure(EmuError.image("再開するダウンロードのサイズ上限を超えています。")); downloadTask.cancel()
        }
        progress(fileOffset, expectedTotalBytes)
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(request.url?.scheme?.lowercased() == "https" ? request : nil)
    }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didWriteData bytesWritten: Int64,
                    totalBytesWritten: Int64, totalBytesExpectedToWrite: Int64) {
        if totalBytesWritten > limit || totalBytesExpectedToWrite > limit {
            result = .failure(EmuError.image("ダウンロードのサイズ上限を超えています。")); downloadTask.cancel(); return
        }
        let now = Date()
        if now.timeIntervalSince(lastSpaceCheck) >= 1 {
            lastSpaceCheck = now
            let values = try? FileManager.default.temporaryDirectory.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey])
            if let available = values?.volumeAvailableCapacityForImportantUsage {
                let remaining = max(0, min(limit, totalBytesExpectedToWrite) - totalBytesWritten)
                if available < remaining + (128 << 20) {
                    result = .failure(EmuError.storage("ダウンロード用の空き容量が不足しています。ストレージを空けて再試行してください。"))
                    downloadTask.cancel(); return
                }
            }
        }
        if now.timeIntervalSince(lastProgress) >= 0.25 || totalBytesWritten == totalBytesExpectedToWrite {
            lastProgress = now
            progress(totalBytesWritten, totalBytesExpectedToWrite)
        }
    }
    func urlSession(_ session: URLSession, downloadTask: URLSessionDownloadTask, didFinishDownloadingTo location: URL) {
        do {
            guard let response = downloadTask.response as? HTTPURLResponse,
                  response.url?.scheme?.lowercased() == "https" else { throw EmuError.image("安全なHTTPS応答を確認できませんでした。") }
            guard (200...299).contains(response.statusCode) else {
                throw DownloadHTTPError(status: response.statusCode, retryAfter: response.value(forHTTPHeaderField: "Retry-After"))
            }
            let size = try location.resourceValues(forKeys: [.fileSizeKey]).fileSize ?? 0
            guard size > 0, Int64(size) <= limit else { throw EmuError.image("ダウンロードしたファイルのサイズが不正です。") }
            if response.value(forHTTPHeaderField: "Content-Encoding") == nil &&
                downloadTask.countOfBytesExpectedToReceive > 0 && Int64(size) < downloadTask.countOfBytesExpectedToReceive {
                throw URLError(.networkConnectionLost)
            }
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
