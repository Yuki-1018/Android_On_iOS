import XCTest
@testable import AndroidEmuModels

final class ModelTests: XCTestCase {
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
