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
    @Published private(set) var downloading = false
    private var downloadTask: Task<Void, Never>?
    @Published private(set) var profiles: [AndroidProfile] = []
    @Published private(set) var selectedID: UUID?
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
            if let profile = selectedProfile { manifest = try await ImageStore(root: profile.directory).current() }
        } catch { errorMessage = error.localizedDescription }
    }
    func select(_ id: UUID) {
        guard !importing, profiles.contains(where: { $0.id == id }) else { return }
        selectedID = id; manifest = nil
        UserDefaults.standard.set(id.uuidString, forKey: "selectedProfile")
        Task { await load() }
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
        guard !importing, let profile = selectedProfile, profiles.count > 1 else { return }
        importing = true
        Task {
            defer { importing = false }
            do {
                try await profileStore.remove(profile, remaining: profiles.filter { $0.id != profile.id })
            } catch { errorMessage = error.localizedDescription }
            do {
                profiles = try await profileStore.load(); selectedID = profiles.first?.id
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
            do { manifest = try await ImageStore(root: profile.directory).importDirectory(url) }
            catch { errorMessage = error.localizedDescription }
        }
    }
    func download(_ entry: ImageCatalog.Entry) {
        guard !importing else { return }
        guard profiles.count < 100 else { errorMessage = "プロファイルは最大100件です。"; return }
        importing = true; downloading = true; transferStatus = "ダウンロードを開始中"
        let profile = AndroidProfile(id: UUID(), name: entry.name, legacy: false)
        downloadTask = Task {
            defer { importing = false; downloading = false; downloadTask = nil; transferStatus = "" }
            var committed = false
            defer { if !committed { try? FileManager.default.removeItem(at: profile.directory) } }
            do {
                let imported = try await RemoteImages.install(entry, into: profile) { [weak self] written, expected in
                    Task { @MainActor [weak self] in
                        guard let self, self.downloading, self.downloadTask?.isCancelled == false else { return }
                        if written < 0 { self.transferStatus = "ZIPを展開・イメージを検証中"; return }
                        let amount = ByteCountFormatter.string(fromByteCount: written, countStyle: .file)
                        self.transferStatus = expected > 0 && written >= expected ? "ZIPを展開・イメージを検証中" : "ダウンロード中: \(amount)"
                    }
                }
                try Task.checkCancellation()
                let updated = profiles + [profile]
                try await profileStore.save(updated)
                committed = true; profiles = updated; selectedID = profile.id; manifest = imported
                UserDefaults.standard.set(profile.id.uuidString, forKey: "selectedProfile")
            } catch is CancellationError { }
            catch { errorMessage = error.localizedDescription }
        }
    }
    func cancelDownload() { transferStatus = "キャンセル処理中"; downloadTask?.cancel() }
}
