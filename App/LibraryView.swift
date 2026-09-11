import SwiftUI
import UniformTypeIdentifiers

struct LibraryView: View {
    @ObservedObject var model: LibraryModel
    @ObservedObject var jit: JITCoordinator
    @State private var chooseImage = false
    @State private var launchWhenReady = false
    @State private var confirmReplace = false
    @State private var showRuntime = false
    @StateObject private var runtime = RuntimeModel()
    @Environment(\.scenePhase) private var scenePhase
    var body: some View {
        NavigationStack {
            List {
                Section {
                    Label("Android 5.1.1", systemImage: "apps.iphone")
                        .font(.title2.bold())
                    Text("アプリや設定はこの端末に保存されます").foregroundStyle(.secondary)
                    if let manifest = model.manifest {
                        Label("イメージ取り込み済み", systemImage: "checkmark.circle")
                        Text(manifest.importedAt, style: .date)
                        Text(ByteCountFormatter.string(fromByteCount: manifest.files.reduce(0) { $0 + $1.bytes }, countStyle: .binary))
                    } else { Text("Androidイメージは同梱されていません。") }
                    Button(model.manifest == nil ? "Add Android" : "イメージを置き換える") { if model.manifest != nil { confirmReplace = true } else { chooseImage = true } }
                        .disabled(model.importing || jit.state == .preparing || runtime.controller.started || runtime.preparing)
                    if model.importing { ProgressView("コピー・変換・SHA-256検証中…") }
                } footer: {
                    Text("合法的に利用可能なAPI 22 default / armeabi-v7aイメージのフォルダを選択してください。source.propertiesが必要です。データは端末内に保存されます。")
                }
                Section {
                    Button("Androidを起動", systemImage: "play.fill") {
                        if jit.state == .ready { launch() }
                        else {
                            launchWhenReady = true
                            jit.enable(cache: model.configuration.cache, openStikDebug: true)
                        }
                    }.disabled(model.manifest == nil || model.importing || jit.state == .preparing || runtime.controller.started || runtime.preparing)
                    Text("StikDebugで起動に必要な準備を行います。戻ると自動的にAndroidを開きます。Android画面では3本指を長押しすると操作メニューが開きます。")
                        .font(.footnote).foregroundStyle(.secondary)
                    if jit.state == .preparing { ProgressView("起動準備中…") }
                    if runtime.preparing { ProgressView(runtime.status) }
                    if !runtime.controller.engineAvailable {
                        Text("QEMU frameworkがありません。エンジンを含むIPAをビルドしてください。")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                    NavigationLink("詳細設定") {
                        List {
                            Section("VM Settings") {
                                Picker("Guest RAM", selection: $model.configuration.ram) {
                                    ForEach(VMConfiguration.RAM.allCases, id: \.self) { Text("\(min($0.rawValue, 760)) MiB" + ($0 == .maximum ? "（上限）" : "")).tag($0) }
                                }
                                Picker("TCG cache", selection: $model.configuration.cache) {
                                    ForEach(VMConfiguration.Cache.allCases, id: \.self) { Text("\($0.rawValue) MiB").tag($0) }
                                }.disabled(jit.state == .preparing || jit.state == .ready)
                                LabeledContent("vCPU", value: "1")
                                Picker("描画する画面幅", selection: $model.configuration.resolution) {
                                    ForEach(VMConfiguration.Resolution.allCases, id: \.self) { resolution in
                                        Text("\(resolution.width) px" + (resolution == .performance ? "（速度優先）" : "")).tag(resolution)
                                    }
                                }.disabled(runtime.controller.started)
                                Text("高さは端末の比率に合わせ、全画面に拡大します。360 pxは540 pxに比べ描画画素数を約56%削減します。")
                                    .font(.footnote).foregroundStyle(.secondary)
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
                            NavigationLink("診断・ログ") { DiagnosticsView(model: model, jit: jit) }
                        }.navigationTitle("詳細設定")
                    }
                }
            }
            .navigationTitle("AndroidEmu")
            .fullScreenCover(isPresented: $showRuntime) { RuntimeView(runtime: runtime) }
            .sheet(isPresented: $chooseImage) { ImageDirectoryPicker { model.importImage($0) } }
            .task { await model.load(); jit.refresh() }
            .confirmationDialog("イメージを置き換えると、保存したAndroidのアプリとデータも置き換わります。", isPresented: $confirmReplace, titleVisibility: .visible) {
                Button("置き換える", role: .destructive) { chooseImage = true }
            }
            .onChange(of: jit.state) { _, state in
                if state == .ready && launchWhenReady && scenePhase == .active { launchWhenReady = false; launch() }
                if state == .failed { launchWhenReady = false; model.errorMessage = jit.detail }
            }
            .onChange(of: scenePhase) { _, phase in if phase == .active {
                jit.refresh()
                if launchWhenReady && jit.state == .ready { launchWhenReady = false; launch() }
            } }
            .alert("処理できませんでした", isPresented: Binding(get: { model.errorMessage != nil }, set: { if !$0 { model.errorMessage = nil } })) {
                Button("OK") { model.errorMessage = nil }
            } message: { Text(model.errorMessage ?? "") }
        }
    }
    private func launch() {
        Task {
            if await runtime.start(configuration: model.configuration) { showRuntime = true }
            else { model.errorMessage = runtime.status }
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
