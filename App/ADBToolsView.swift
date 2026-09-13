import SwiftUI
import Combine
import UniformTypeIdentifiers

struct ADBToolsView: View {
    let client: AEADBClient
    let paused: Bool
    let resume: () -> Void
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
            if paused {
                Section {
                    Text("Androidが一時停止しています。ADBを使うには実行を再開してください。")
                    Button("Androidを再開", action: resume)
                }
            }
            Section {
                Text(status)
                if busy { ProgressView(value: progress).accessibilityLabel("APK転送") }
                Text("初回接続ではAndroid側の「USBデバッグを許可」を承認してください。")
                    .font(.footnote).foregroundStyle(.secondary)
                Button("起動完了を確認") { client.checkBoot() }.disabled(busy)
                Button("APKをインストール", systemImage: "square.and.arrow.down") { chooseAPK = true }.disabled(busy)
                Button("起動診断を取得") {
                    client.runShell("echo '=== boot ==='; getprop sys.boot_completed; getprop init.svc.bootanim; getprop init.svc.zygote; getprop init.svc.surfaceflinger; echo '=== uptime ==='; cat /proc/uptime; echo '=== memory ==='; cat /proc/meminfo; echo '=== processes ==='; ps; echo '=== errors ==='; logcat -b main -b system -b crash -d -t 200")
                }.disabled(busy)
                Button("最近のlogcat") { client.runShell("logcat -d -t 200") }.disabled(busy)
                Button("アプリ停止の診断を取得（Android 6）") {
                    client.runShell("getprop ro.build.fingerprint; getprop ro.product.cpu.abi; getprop ro.opengles.version; getprop ro.hardware.egl; getprop init.svc.webview_zygote; cat /proc/meminfo; cat /proc/mounts; df; dumpsys webviewupdate; logcat -b crash -d -t 300; logcat -b main -b system -d -t 500")
                }.disabled(busy)
                Button("応答停止（ANR）の診断を取得") {
                    client.runShell("echo '=== memory ==='; cat /proc/meminfo; echo '=== activity ==='; dumpsys activity lastanr; echo '=== thread traces ==='; head -n 400 /data/anr/traces.txt; echo '=== system log ==='; logcat -b system -b main -d -t 500")
                }.disabled(busy)
                Button("ADB接続を確認") { client.runShell("echo AndroidEmu_ADB_OK") }.disabled(busy)
            }.disabled(paused)
            Section("Android shell") {
                TextField("コマンド", text: $command).textInputAutocapitalization(.never).autocorrectionDisabled()
                Button("実行") { client.runShell(command) }.disabled(busy || command.isEmpty)
            }.disabled(paused)
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
