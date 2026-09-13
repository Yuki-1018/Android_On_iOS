import XCTest
@testable import AndroidEmuModels

final class ModelTests: XCTestCase {
    func testEmptyLibraryAndLastProfileRemoval() async throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let store = ProfileStore(root: root)
        let empty = try await store.load()
        XCTAssertTrue(empty.isEmpty)
        let profile = AndroidProfile(id: UUID(), name: "Android", legacy: false)
        try await store.save([profile])
        try await store.remove(profile, remaining: [])
        let reopened = try await ProfileStore(root: root).load()
        XCTAssertTrue(reopened.isEmpty)
    }
    func testDownloadRetryPolicy() {
        XCTAssertEqual(DownloadRetry.delay(error: URLError(.networkConnectionLost), failedAttempt: 1), 1)
        XCTAssertEqual(DownloadRetry.delay(error: URLError(.timedOut), failedAttempt: 3), 4)
        XCTAssertNil(DownloadRetry.delay(error: URLError(.networkConnectionLost), failedAttempt: 4))
        XCTAssertNil(DownloadRetry.delay(error: URLError(.cancelled), failedAttempt: 1))
        XCTAssertNil(DownloadRetry.delay(error: URLError(.serverCertificateUntrusted), failedAttempt: 1))
        XCTAssertNil(DownloadRetry.delay(error: DownloadHTTPError(status: 404, retryAfter: nil), failedAttempt: 1))
        XCTAssertEqual(DownloadRetry.delay(error: DownloadHTTPError(status: 503, retryAfter: "12"), failedAttempt: 1), 12)
        XCTAssertNil(DownloadRetry.delay(error: DownloadHTTPError(status: 429, retryAfter: "3600"), failedAttempt: 1))
        XCTAssertNil(DownloadRetry.delay(error: EmuError.storage("full"), failedAttempt: 1))
    }
    func testRetryCarriesResumeDataAndStopsAtLimit() async throws {
        actor Attempts {
            var count = 0
            var tokens: [Data?] = []
            func run(_ token: Data?) throws -> URL {
                count += 1; tokens.append(token)
                if count == 1 { throw NSError(domain: NSURLErrorDomain, code: URLError.networkConnectionLost.rawValue,
                                              userInfo: [NSURLSessionDownloadTaskResumeData: Data([1, 2, 3])]) }
                return URL(fileURLWithPath: "/tmp/complete")
            }
        }
        let attempts = Attempts()
        let url = try await DownloadRetry.perform(progress: { _, _ in }, sleep: { _ in }, operation: { try await attempts.run($0) })
        XCTAssertEqual(url.lastPathComponent, "complete")
        let tokens = await attempts.tokens
        XCTAssertEqual(tokens.count, 2)
        XCTAssertNil(tokens[0]); XCTAssertEqual(tokens[1], Data([1, 2, 3]))
        actor Failures {
            var count = 0
            func run() throws -> URL { count += 1; throw URLError(.timedOut) }
        }
        let failures = Failures()
        do {
            _ = try await DownloadRetry.perform(progress: { _, _ in }, sleep: { _ in }, operation: { _ in try await failures.run() })
            XCTFail("Retry loop must terminate")
        } catch { XCTAssertEqual((error as NSError).code, URLError.timedOut.rawValue) }
        let count = await failures.count
        XCTAssertEqual(count, 4)
    }
    func testProfileCatalogMigrationAndRemoval() async throws {
        let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? FileManager.default.removeItem(at: root) }
        let old = root.appendingPathComponent("Android51")
        try FileManager.default.createDirectory(at: old, withIntermediateDirectories: true)
        try Data("legacy".utf8).write(to: old.appendingPathComponent("image.json"))
        let store = ProfileStore(root: root)
        let original = try await store.load()
        XCTAssertEqual(original.count, 1)
        XCTAssertTrue(original[0].legacy)
        let extra = AndroidProfile(id: UUID(), name: "Android 6", legacy: false)
        try await store.save(original + [extra])
        let reopened = try await ProfileStore(root: root).load()
        XCTAssertEqual(reopened, original + [extra])
        try FileManager.default.createDirectory(at: extra.directory(in: root), withIntermediateDirectories: true)
        try Data("new".utf8).write(to: extra.directory(in: root).appendingPathComponent("userdata.img"))
        try await store.remove(extra, remaining: original)
        XCTAssertFalse(FileManager.default.fileExists(atPath: extra.directory(in: root).path))
        XCTAssertEqual(try Data(contentsOf: old.appendingPathComponent("image.json")), Data("legacy".utf8))
    }
    func testIndependentProfileDirectoriesAndLegacyData() throws {
        let first = AndroidProfile(id: UUID(), name: "Android 5.1", legacy: false)
        let second = AndroidProfile(id: UUID(), name: "Android 6", legacy: false)
        XCTAssertNotEqual(first.directory, second.directory)
        var renamed = first; renamed.name = "../../test"
        XCTAssertEqual(first.directory, renamed.directory)
        let legacy = AndroidProfile(id: UUID(), name: "旧データ", legacy: true)
        XCTAssertEqual(legacy.directory.lastPathComponent, "Android51")
        XCTAssertEqual(try JSONDecoder().decode(AndroidProfile.self, from: JSONEncoder().encode(first)), first)
    }
    func testDownloadCatalog() throws {
        let valid = Data(#"{"images":[{"name":"Android 6.0","url":"https://example.com/android6.zip"}]}"#.utf8)
        XCTAssertEqual(try ImageCatalog.decode(valid).first?.name, "Android 6.0")
        let unsafe = Data(#"{"images":[{"name":"Bad","url":"file:///tmp/image.zip"}]}"#.utf8)
        XCTAssertThrowsError(try ImageCatalog.decode(unsafe))
        XCTAssertThrowsError(try ImageCatalog.decode(Data("not json".utf8)))
        XCTAssertThrowsError(try ImageCatalog.decode(Data(#"{"images":[{"name":"Bad","url":"http://example.com/a.zip"}]}"#.utf8)))
    }
    func testStikDebugURL() throws {
        let url = try JITRequest.url(bundleID: "org.example.test", pid: 123, txm: .present, sptm: .present)
        let parts = try XCTUnwrap(URLComponents(url: url, resolvingAgainstBaseURL: false))
        XCTAssertEqual(parts.scheme, "stikdebug")
        XCTAssertEqual(parts.host, "enable-jit")
        XCTAssertEqual(parts.queryItems?.first(where: { $0.name == "pid" })?.value, "123")
        XCTAssertEqual(parts.queryItems?.first(where: { $0.name == "script-name" })?.value, "universal.js")
        let simple = try JITRequest.url(bundleID: "org.example.test", pid: 123, txm: .absent, sptm: .absent)
        XCTAssertFalse(simple.absoluteString.contains("script-name"))
        XCTAssertThrowsError(try JITRequest.url(bundleID: "org.example.test", pid: 123, txm: .unknown, sptm: .present))
        XCTAssertThrowsError(try JITRequest.url(bundleID: "", pid: 0, txm: .absent, sptm: .absent))
    }
    func testConfigurationRoundTrip() throws {
        let config = VMConfiguration()
        XCTAssertEqual(try JSONDecoder().decode(VMConfiguration.self, from: JSONEncoder().encode(config)), config)
        var invalid = config; invalid.cpuCount = 4
        XCTAssertThrowsError(try invalid.validate())
    }
    func testArbitraryMemoryAndLegacyProfiles() throws {
        for value in [512, 640, 768, 1024, 1537, 2048, 4080, 4096] {
            let ram = try JSONDecoder().decode(VMConfiguration.RAM.self, from: Data("\(value)".utf8))
            XCTAssertEqual(ram.rawValue, value)
            XCTAssertEqual(try JSONEncoder().encode(ram), Data("\(value)".utf8))
            XCTAssertEqual(ram.guestMiB, min(value, 4080))
            XCTAssertEqual(ram.needsHighmemKernel, value > 760)
        }
        for value in [0, 511, 4097, Int.max] {
            XCTAssertNil(VMConfiguration.RAM(rawValue: value))
            XCTAssertThrowsError(try JSONDecoder().decode(VMConfiguration.RAM.self, from: Data("\(value)".utf8)))
        }
    }
    func testFixedProfile() throws {
        let text = "AndroidVersion.ApiLevel=22\nSystemImage.Abi=armeabi-v7a\nSystemImage.TagId=default\n"
        let properties = try ImageProfile.properties(text)
        XCTAssertNoThrow(try ImageProfile.validate(properties))
        var wrong = properties; wrong["SystemImage.Abi"] = "arm64-v8a"
        XCTAssertThrowsError(try ImageProfile.validate(wrong))
        wrong = properties; wrong["AndroidVersion.ApiLevel"] = "24"
        XCTAssertThrowsError(try ImageProfile.validate(wrong))
        XCTAssertThrowsError(try ImageProfile.validate([:]))
        XCTAssertThrowsError(try ImageProfile.properties("a=1\na=2"))
    }
    func testAndroidProfilesAndLegacyManifest() throws {
        XCTAssertEqual(ImageProfile.api(for: "android-5.1.1-api22-armv7-goldfish"), 22)
        for api in [14, 15, 16, 17, 18, 19, 21, 22, 23] {
            var values = ["AndroidVersion.ApiLevel": String(api), "SystemImage.Abi": "armeabi-v7a", "SystemImage.TagId": "default"]
            XCTAssertNoThrow(try ImageProfile.validate(values))
            XCTAssertEqual(ImageProfile.api(for: ImageProfile.identifier(api: api)), api)
            values["SystemImage.Abi"] = "arm64-v8a"
            XCTAssertThrowsError(try ImageProfile.validate(values))
        }
        XCTAssertNoThrow(try ImageProfile.validate(["AndroidVersion.ApiLevel": "16", "SystemImage.Abi": "armeabi-v7a"]))
        XCTAssertThrowsError(try ImageProfile.validate(["AndroidVersion.ApiLevel": "23", "SystemImage.Abi": "armeabi-v7a"]))
        let legacyURL = try JITRequest.url(bundleID: "org.example.test", pid: 123, requiresProtocol: false)
        XCTAssertFalse(legacyURL.absoluteString.contains("universal.js"))
        let modernURL = try JITRequest.url(bundleID: "org.example.test", pid: 123, requiresProtocol: true)
        XCTAssertTrue(modernURL.absoluteString.contains("universal.js"))
    }

}
