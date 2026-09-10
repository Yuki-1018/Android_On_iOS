import SwiftUI
import Combine
import UniformTypeIdentifiers

struct ADBToolsView: View {
    let client: AEADBClient
    @State private var status = "adbd待機中"
    @State private var output = ""
    @State private var busy = false
    @State private var progress = 0.0
    @State private var chooseAPK = false
    @State private var command = ""
    @State private var pickerError: String?
    private let timer = Timer.publish(every: 0.5, on: .main, in: .common).autoconnect()
    var body: some View {
        List {
            Section {
                Text(status)
                if busy { ProgressView(value: progress).accessibilityLabel("APK転送") }
                Text("初回接続ではAndroid側の「USBデバッグを許可」を承認してください。")
                    .font(.footnote).foregroundStyle(.secondary)
                Button("起動完了を確認") { client.checkBoot() }.disabled(busy)
                Button("APKをインストール", systemImage: "square.and.arrow.down") { chooseAPK = true }.disabled(busy)
                Button("最近のlogcat") { client.runShell("logcat -d -t 200") }.disabled(busy)
            }
            Section("Android shell") {
                TextField("コマンド", text: $command).textInputAutocapitalization(.never).autocorrectionDisabled()
                Button("実行") { client.runShell(command) }.disabled(busy || command.isEmpty)
            }
            Section("出力") {
                Text(output).font(.caption.monospaced()).textSelection(.enabled)
                if !output.isEmpty { ShareLink(item: output) }
            }
        }
        .navigationTitle("APK・ADB")
        .onReceive(timer) { _ in
            status = client.statusText; output = client.outputText
            busy = client.busy; progress = client.transferProgress
        }
        .fileImporter(isPresented: $chooseAPK, allowedContentTypes: [UTType(filenameExtension: "apk") ?? .data]) { result in
            switch result {
            case .success(let url): client.installAPK(url)
            case .failure(let error): pickerError = error.localizedDescription
            }
        }
        .alert("APKを開けませんでした", isPresented: Binding(get: { pickerError != nil }, set: { if !$0 { pickerError = nil } })) {
            Button("OK") { pickerError = nil }
        } message: { Text(pickerError ?? "") }
    }
}
