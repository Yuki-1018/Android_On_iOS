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
    private let images = ImageStore()
    init() {
        if let data = UserDefaults.standard.data(forKey: "configuration"),
           let decoded = try? JSONDecoder().decode(VMConfiguration.self, from: data),
           (try? decoded.validate()) != nil { configuration = decoded }
        else { configuration = VMConfiguration() }
    }
    func load() async {
        do { manifest = try await images.current() } catch { errorMessage = error.localizedDescription }
    }
    func importImage(_ url: URL) {
        guard !importing else { return }
        importing = true
        Task {
            defer { importing = false }
            do { manifest = try await images.importDirectory(url) }
            catch { errorMessage = error.localizedDescription }
        }
    }
}
