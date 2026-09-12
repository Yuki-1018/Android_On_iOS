import Foundation

struct DownloadHTTPError: LocalizedError {
    let status: Int
    let retryAfter: String?
    var errorDescription: String? {
        switch status {
        case 404: "イメージが見つかりません（HTTP 404）。一覧を更新してください。"
        case 401, 403: "ダウンロードが許可されていません（HTTP \(status)）。"
        case 429: "サーバーが混雑しています（HTTP 429）。"
        default: "サーバーとの通信に失敗しました（HTTP \(status)）。"
        }
    }
}

enum DownloadRetry {
    static let maximumAttempts = 4
    static func delay(error: Error, failedAttempt: Int, now: Date = Date()) -> TimeInterval? {
        guard failedAttempt > 0, failedAttempt < maximumAttempts else { return nil }
        if error is CancellationError { return nil }
        if let http = error as? DownloadHTTPError {
            guard [408, 429, 500, 502, 503, 504].contains(http.status) else { return nil }
            if let text = http.retryAfter {
                var seconds = TimeInterval(text)
                if seconds == nil {
                    let formatter = DateFormatter()
                    formatter.locale = Locale(identifier: "en_US_POSIX")
                    formatter.timeZone = TimeZone(secondsFromGMT: 0)
                    formatter.dateFormat = "EEE, dd MMM yyyy HH:mm:ss zzz"
                    seconds = formatter.date(from: text)?.timeIntervalSince(now)
                }
                // Never retry earlier than a server's Retry-After. Long delays
                // return control to the user rather than keeping a job asleep.
                if let seconds, seconds.isFinite {
                    guard seconds <= 60 else { return nil }
                    return max(1, seconds)
                }
            }
        } else {
            let ns = error as NSError
            guard ns.domain == NSURLErrorDomain,
                  [URLError.Code.timedOut, .networkConnectionLost, .notConnectedToInternet,
                   .cannotConnectToHost, .cannotFindHost, .dnsLookupFailed, .resourceUnavailable]
                    .map(\.rawValue).contains(ns.code) else { return nil }
        }
        return pow(2, Double(failedAttempt - 1))
    }
    static func fetch(_ url: URL, limit: Int64,
                      progress: @escaping @Sendable (Int64, Int64) -> Void) async throws -> URL {
        try await perform(progress: progress) { resumeData in
            try await BoundedDownload(limit: limit, progress: progress, resumeData: resumeData).run(url)
        }
    }
    static func perform(progress: @escaping @Sendable (Int64, Int64) -> Void,
                        sleep: @escaping @Sendable (TimeInterval) async throws -> Void = { try await Task.sleep(for: .seconds($0)) },
                        operation: @escaping @Sendable (Data?) async throws -> URL) async throws -> URL {
        var resumeData: Data?
        for attempt in 1...maximumAttempts {
            try Task.checkCancellation()
            do {
                return try await operation(resumeData)
            } catch {
                try Task.checkCancellation()
                guard let delay = delay(error: error, failedAttempt: attempt) else { throw error }
                // Resume data is produced by this URLSession transfer only;
                // never accept a resume token from the catalog or filesystem.
                resumeData = (error as NSError).userInfo[NSURLSessionDownloadTaskResumeData] as? Data
                if let data = resumeData, data.count > 1 << 20 { resumeData = nil }
                progress(-3, Int64(attempt))
                try await sleep(delay)
            }
        }
        throw EmuError.image("再試行の上限に達しました。")
    }
}
