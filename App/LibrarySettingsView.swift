import SwiftUI

struct LibrarySettingsView: View {
    @ObservedObject var model: LibraryModel
    @ObservedObject var jit: JITCoordinator
    let running: Bool
    var body: some View {
        List {
            Section("VM Settings") {
                Picker("Guest RAM", selection: $model.configuration.ram) {
                    ForEach(VMConfiguration.RAM.allCases, id: \.self) { Text("\(min($0.rawValue, 760)) MiB" + ($0 == .maximum ? "（上限）" : "")).tag($0) }
                }
                Picker("TCG cache", selection: $model.configuration.cache) {
                    ForEach(VMConfiguration.Cache.allCases, id: \.self) { Text("\($0.rawValue) MiB").tag($0) }
                }.disabled(jit.state == .preparing || jit.state == .ready)
                LabeledContent("vCPU", value: "1")
                Text("拡張メモリの資格を持つ署名と十分な空きメモリがある場合、変換キャッシュを自動で最大512 MiBに拡張します。AndroidのRAMは最大760 MiBです。RAM 3 GB以下の端末では安定動作のため、AndroidのRAMを最大640 MiB、変換キャッシュを最大128 MiBへ自動調整します。")
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
    }
}
