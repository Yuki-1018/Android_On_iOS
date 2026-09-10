import SwiftUI
import UniformTypeIdentifiers

struct LibraryView: View {
    @ObservedObject var model: LibraryModel
    @ObservedObject var jit: JITCoordinator
    @State private var chooseImage = false
    @State private var showRuntime = false
    @StateObject private var runtime = RuntimeModel()
    @Environment(\.scenePhase) private var scenePhase
    var body: some View {
        NavigationStack {
            List {
                Section {
                    Label("Android 5.1.1", systemImage: "apps.iphone")
                        .font(.title2.bold())
                    Text("API 22 · ARMv7 · Goldfish").foregroundStyle(.secondary)
                    if let manifest = model.manifest {
                        Label("イメージ取り込み済み", systemImage: "checkmark.circle")
                        Text(manifest.importedAt, style: .date)
                        Text(ByteCountFormatter.string(fromByteCount: manifest.files.reduce(0) { $0 + $1.bytes }, countStyle: .binary))
                    } else { Text("Androidイメージは同梱されていません。") }
                    Button(model.manifest == nil ? "Add Android" : "イメージを置き換える") { chooseImage = true }
                        .disabled(model.importing || jit.state == .preparing || runtime.controller.started)
                    if model.importing { ProgressView("コピー・変換・SHA-256検証中…") }
                } footer: {
                    Text("合法的に利用可能なAPI 22 default / armeabi-v7aイメージのフォルダを選択してください。source.propertiesが必要です。データは端末内に保存されます。")
                }
                Section("VM Settings") {
                    Picker("Guest RAM", selection: $model.configuration.ram) {
                        ForEach(VMConfiguration.RAM.allCases, id: \.self) { Text("\($0.rawValue) MiB").tag($0) }
                    }
                    Picker("TCG cache", selection: $model.configuration.cache) {
                        ForEach(VMConfiguration.Cache.allCases, id: \.self) { Text("\($0.rawValue) MiB").tag($0) }
                    }.disabled(jit.state == .preparing || jit.state == .ready)
                    LabeledContent("vCPU", value: "1")
                    LabeledContent("Resolution", value: "端末の画面に合わせる")
                }
                Section("JIT") {
                    LabeledContent("Status", value: jit.state.rawValue)
                    LabeledContent("TXM", value: jit.txm.label)
                    LabeledContent("SPTM", value: jit.sptm.label)
                    LabeledContent("get-task-allow", value: jit.entitlement ? "Available" : "Missing")
                    Text(jit.detail).font(.footnote).textSelection(.enabled)
                    Button("Enable with StikDebug") { jit.enable(cache: model.configuration.cache, openStikDebug: true) }
                        .disabled(jit.state == .preparing || jit.state == .ready)
                    Button("Wait for Compatible Debugger") { jit.enable(cache: model.configuration.cache, openStikDebug: false) }
                        .disabled(jit.state == .preparing || jit.state == .ready)
                }
                Section {
                    Button("Androidを起動", systemImage: "play.fill") {
                        if runtime.start(configuration: model.configuration) { showRuntime = true }
                        else { model.errorMessage = runtime.status }
                    }.disabled(model.manifest == nil || model.importing || jit.state != .ready || runtime.controller.started)
                    if !runtime.controller.engineAvailable {
                        Text("QEMU frameworkがありません。エンジンを含むIPAをビルドしてください。")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                    NavigationLink("Diagnostics") { DiagnosticsView(model: model, jit: jit) }
                }
            }
            .navigationTitle("AndroidEmu")
            .fullScreenCover(isPresented: $showRuntime) { RuntimeView(runtime: runtime) }
            .sheet(isPresented: $chooseImage) { ImageDirectoryPicker { model.importImage($0) } }
            .task { await model.load(); jit.refresh() }
            .onChange(of: scenePhase) { _, phase in if phase == .active { jit.refresh() } }
            .alert("処理できませんでした", isPresented: Binding(get: { model.errorMessage != nil }, set: { if !$0 { model.errorMessage = nil } })) {
                Button("OK") { model.errorMessage = nil }
            } message: { Text(model.errorMessage ?? "") }
        }
    }
}

private struct ImageDirectoryPicker: UIViewControllerRepresentable {
    var selected: (URL) -> Void
    @Environment(\.dismiss) private var dismiss
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: [.folder], asCopy: false)
        picker.allowsMultipleSelection = false; picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: UIDocumentPickerViewController, context: Context) {}
    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let parent: ImageDirectoryPicker
        init(_ parent: ImageDirectoryPicker) { self.parent = parent }
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            if let url = urls.first { parent.selected(url) }; parent.dismiss()
        }
        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { parent.dismiss() }
    }
}
