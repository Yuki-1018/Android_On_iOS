import SwiftUI

struct LibrarySettingsView: View {
    @ObservedObject var model: LibraryModel
    @ObservedObject var jit: JITCoordinator
    let running: Bool
    @State private var ramInput = ""
    @State private var ramError: String?
    var body: some View {
        List {
            Section("VM Settings") {
                LabeledContent("Androidのメモリ", value: "\(model.configuration.ram.rawValue) MiB")
                Slider(value: Binding(get: { Double(model.configuration.ram.rawValue) }, set: {
                    if let ram = VMConfiguration.RAM(rawValue: Int($0)) { model.configuration.ram = ram }
                }), in: 512...4096, step: 1).disabled(running)
                HStack {
                    TextField("512〜4096", text: $ramInput).keyboardType(.numberPad)
                        .accessibilityLabel("メモリ容量（MiB）")
                    Text("MiB").foregroundStyle(.secondary)
                    Button("適用") {
                        guard let value = Int(ramInput), let ram = VMConfiguration.RAM(rawValue: value) else {
                            ramError = "512〜4096の整数を入力してください。"; return
                        }
                        model.configuration.ram = ram; ramError = nil
                    }.buttonStyle(.bordered)
                }.disabled(running)
                if let ramError { Text(ramError).foregroundStyle(.red).font(.footnote) }
                Text("次回起動時に使用: \(model.configuration.ram.guestMiB) MiB" +
                     (model.configuration.ram.needsHighmemKernel ? "（HIGHMEM対応カーネル）" : "（イメージ付属カーネル）"))
                    .font(.footnote).foregroundStyle(.secondary)
                Picker("TCG cache", selection: $model.configuration.cache) {
                    ForEach(VMConfiguration.Cache.allCases, id: \.self) { Text("\($0.rawValue) MiB").tag($0) }
                }.disabled(jit.state == .preparing || jit.state == .ready)
                LabeledContent("vCPU", value: "1")
                Text("少ないメモリの端末では640 MiBを推奨します。761 MiB以上では同梱の専用カーネルを使います。32bitボードの機器用領域を除き、Androidが使える上限は4080 MiBです。端末・署名のメモリ上限を超える場合は起動できません。拡張メモリの資格と空きメモリがある場合、変換キャッシュは自動で最大512 MiBになります。")
                    .font(.footnote).foregroundStyle(.secondary)
                Picker("描画する画面幅", selection: $model.configuration.resolution) {
                    ForEach(VMConfiguration.Resolution.allCases, id: \.self) { resolution in
                        Text("\(resolution.width) px" + (resolution == .performance ? "（速度優先）" : "")).tag(resolution)
                    }
                }.disabled(running)
                Text("高さは端末の比率に合わせ、全画面に拡大します。360 pxは540 pxに比べ描画画素数を約56%削減します。")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            Section("JIT") {
                LabeledContent("Status", value: jit.state.rawValue)
                LabeledContent("TXM", value: jit.txm.label)
                LabeledContent("SPTM", value: jit.sptm.label)
                LabeledContent("get-task-allow", value: jit.entitlement ? "Available" : "Missing")
                Text(jit.detail).font(.footnote).textSelection(.enabled)
                Button("Wait for Compatible Debugger") { jit.enable(cache: model.configuration.cache, openStikDebug: false) }
                    .disabled(jit.state == .preparing || jit.state == .ready)
            }
            NavigationLink("診断・ログ") { DiagnosticsView(model: model, jit: jit) }
        }.navigationTitle("詳細設定")
            .onAppear { ramInput = String(model.configuration.ram.rawValue) }
            .onChange(of: model.configuration.ram) { _, ram in ramInput = String(ram.rawValue); ramError = nil }
    }
}
