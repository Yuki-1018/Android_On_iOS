import SwiftUI

@main struct AndroidEmuApp: App {
    @StateObject private var model = LibraryModel()
    @StateObject private var jit = JITCoordinator()
    var body: some Scene {
        WindowGroup { LibraryView(model: model, jit: jit) }
    }
}

@MainActor final class LibraryModel: ObservableObject {
    @Published var configuration: VMConfiguration {
        didSet {
            do {
                try configuration.validate()
                UserDefaults.standard.set(try JSONEncoder().encode(configuration), forKey: "configuration")
            } catch { errorMessage = error.localizedDescription }
        }
    }
    @Published private(set) var manifest: ImageManifest?
    @Published private(set) var importing = false
    @Published var errorMessage: String?
    @Published private(set) var transferStatus = ""
    @Published private(set) var transferProgress: Double?
    @Published private(set) var downloading = false
    private var downloadTask: Task<Void, Never>?
    @Published private(set) var profiles: [AndroidProfile] = []
    @Published private(set) var selectedID: UUID?
    @Published private(set) var profileImages: [UUID: ImageManifest] = [:]
    @Published private(set) var profileIssues: [UUID: String] = [:]
    var selectedProfile: AndroidProfile? { profiles.first { $0.id == selectedID } }
    private let profileStore = ProfileStore()
    init() {
        if let data = UserDefaults.standard.data(forKey: "configuration"),
           let decoded = try? JSONDecoder().decode(VMConfiguration.self, from: data),
           (try? decoded.validate()) != nil { configuration = decoded }
        else { configuration = VMConfiguration() }
    }
    func load() async {
        guard !importing else { return }
        importing = true
        defer { importing = false }
        do {
            manifest = nil
            profiles = try await profileStore.load()
            let saved = UserDefaults.standard.string(forKey: "selectedProfile").flatMap(UUID.init(uuidString:))
            selectedID = profiles.first { $0.id == (selectedID ?? saved) }?.id ?? profiles.first?.id
            var images: [UUID: ImageManifest] = [:]
            var issues: [UUID: String] = [:]
            for profile in profiles {
                do { images[profile.id] = try await ImageStore(root: profile.directory).current() }
                catch { issues[profile.id] = error.localizedDescription }
            }
            profileImages = images; profileIssues = issues
            manifest = selectedID.flatMap { images[$0] }
        } catch { errorMessage = error.localizedDescription }
    }
    func select(_ id: UUID) {
        guard !importing, profiles.contains(where: { $0.id == id }) else { return }
        selectedID = id; manifest = profileImages[id]
        UserDefaults.standard.set(id.uuidString, forKey: "selectedProfile")
    }
    func saveProfile(name: String, creating: Bool) {
        let name = String(name.trimmingCharacters(in: .whitespacesAndNewlines).prefix(80))
        guard !importing, !name.isEmpty else { return }
        importing = true
        Task {
            defer { importing = false }
            do {
                var updated = profiles
                var selected = selectedID
                if creating {
                    guard updated.count < 100 else { throw EmuError.storage("プロファイルは最大100件です。") }
                    let profile = AndroidProfile(id: UUID(), name: name, legacy: false)
                    updated.append(profile); selected = profile.id
                } else if let at = updated.firstIndex(where: { $0.id == selectedID }) { updated[at].name = name }
                try await profileStore.save(updated)
                profiles = updated; selectedID = selected
                UserDefaults.standard.set(selected?.uuidString, forKey: "selectedProfile")
                if creating { manifest = nil }
            } catch { errorMessage = error.localizedDescription }
        }
    }
    func deleteProfile() {
        guard !importing, let profile = selectedProfile else { return }
        importing = true
        Task {
            defer { importing = false }
            do {
                try await profileStore.remove(profile, remaining: profiles.filter { $0.id != profile.id })
            } catch { errorMessage = error.localizedDescription }
            do {
                profiles = try await profileStore.load(); selectedID = profiles.first?.id
                profileImages = profileImages.filter { id, _ in profiles.contains { $0.id == id } }
                profileIssues = profileIssues.filter { id, _ in profiles.contains { $0.id == id } }
                UserDefaults.standard.set(selectedID?.uuidString, forKey: "selectedProfile")
                manifest = nil
                if let profile = selectedProfile { manifest = try await ImageStore(root: profile.directory).current() }
            } catch { errorMessage = error.localizedDescription }
        }
    }
    func importImage(_ url: URL) {
        guard !importing, let profile = selectedProfile else { return }
        importing = true
        Task {
            defer { importing = false }
            do {
                manifest = try await ImageStore(root: profile.directory).importDirectory(url)
                profileImages[profile.id] = manifest; profileIssues[profile.id] = nil
            }
            catch { errorMessage = error.localizedDescription }
        }
    }
    func download(_ entry: ImageCatalog.Entry, name: String = "") {
        guard !importing else { return }
        guard profiles.count < 100 else { errorMessage = "プロファイルは最大100件です。"; return }
        errorMessage = nil
        importing = true; downloading = true; transferProgress = nil; transferStatus = "ダウンロードを開始中"
        let profile = AndroidProfile(id: UUID(), name: Self.importName(name, fallback: entry.name), legacy: false)
        downloadTask = Task {
            defer { importing = false; downloading = false; downloadTask = nil; transferStatus = ""; transferProgress = nil }
            var committed = false
            defer { if !committed { try? FileManager.default.removeItem(at: profile.directory) } }
            do {
                let imported = try await RemoteImages.install(entry, into: profile) { [weak self] written, expected in
                    Task { @MainActor [weak self] in
                        guard let self, self.downloading, self.downloadTask?.isCancelled == false else { return }
                        self.transferProgress = written >= 0 && expected > 0 ? min(1, Double(written) / Double(expected)) : nil
                        if written == -2 { self.transferStatus = "ネットワークへの接続を待っています"; return }
                        if written == -3 { self.transferStatus = "通信を再試行中（\(expected)/3）"; return }
                        if written < 0 { self.transferStatus = "ZIPを展開・イメージを検証中"; return }
                        let amount = ByteCountFormatter.string(fromByteCount: written, countStyle: .file)
                        self.transferStatus = expected > 0 && written >= expected ? "ZIPを展開・イメージを検証中" : "ダウンロード中: \(amount)"
                    }
                }
                try Task.checkCancellation()
                let updated = profiles + [profile]
                try await profileStore.save(updated)
                committed = true; profiles = updated; selectedID = profile.id; manifest = imported
                profileImages[profile.id] = imported
                UserDefaults.standard.set(profile.id.uuidString, forKey: "selectedProfile")
            } catch is CancellationError { }
            catch { errorMessage = error.localizedDescription }
        }
    }
    private static func importName(_ name: String, fallback: String) -> String {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        return String((trimmed.isEmpty ? (fallback.isEmpty ? "Android" : fallback) : trimmed).prefix(80))
    }
    func cancelDownload() { transferStatus = "キャンセル処理中"; downloadTask?.cancel() }
    func importNewImage(_ url: URL, name: String = "") {
        guard !importing else { return }
        guard profiles.count < 100 else { errorMessage = "プロファイルは最大100件です。"; return }
        importing = true
        errorMessage = nil
        let name = Self.importName(name, fallback: url.lastPathComponent)
        let profile = AndroidProfile(id: UUID(), name: name.isEmpty ? "Android" : name, legacy: false)
        Task {
            defer { importing = false }
            var committed = false
            defer { if !committed { try? FileManager.default.removeItem(at: profile.directory) } }
            do {
                let imported = try await ImageStore(root: profile.directory).importDirectory(url)
                try await profileStore.save(profiles + [profile])
                committed = true; profiles.append(profile); selectedID = profile.id
                manifest = imported; profileImages[profile.id] = imported
                UserDefaults.standard.set(profile.id.uuidString, forKey: "selectedProfile")
            } catch { errorMessage = error.localizedDescription }
        }
    }
}
