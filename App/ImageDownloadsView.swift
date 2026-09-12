import SwiftUI

struct ImageDownloadsView: View {
    @ObservedObject var model: LibraryModel
    var profileName: String = ""
    @State private var entries: [ImageCatalog.Entry] = []
    @State private var loading = false
    @State private var error: String?
    @State private var selection: ImageCatalog.Entry?
    var body: some View {
        List {
            Section {
                Text("ダウンロードしたAndroidは新しいプロファイルへ保存します。自分のイメージをフォルダから取り込む機能も引き続き使えます。")
                if loading { ProgressView("一覧を取得中") }
                if let error { Text(error).foregroundStyle(.secondary) }
                if !loading && entries.isEmpty && error == nil { Text("現在公開されているイメージはありません。") }
                Button("一覧を再読み込み") { Task { await refresh() } }.disabled(loading || model.importing)
            }
            Section("イメージ") {
                ForEach(entries) { entry in
                    Button { selection = entry } label: {
                        VStack(alignment: .leading) {
                            Text(entry.name)
                            Text(entry.url.host ?? "").font(.caption).foregroundStyle(.secondary)
                        }
                    }.disabled(model.importing)
                }
            }
            if let failure = model.errorMessage {
                Section {
                    Label(failure, systemImage: "exclamationmark.triangle").foregroundStyle(.red)
                    Text("通信を確認して、イメージを選び直すと再試行できます。").font(.footnote)
                    Button("エラーを閉じる") { model.errorMessage = nil }
                }
            }
            if model.downloading {
                Section {
                    ProgressView(value: model.transferProgress) { Text(model.transferStatus) }
                    Button("キャンセル", role: .cancel) { model.cancelDownload() }
                }
            }
        }
        .navigationTitle("イメージをダウンロード")
        .task { await refresh() }
        .confirmationDialog(selection?.name ?? "ダウンロード", isPresented: Binding(get: { selection != nil }, set: { if !$0 { selection = nil } }), titleVisibility: .visible) {
            if let entry = selection { Button("このAndroidを追加") { model.download(entry, name: profileName); selection = nil } }
        } message: { Text("利用権限のあるイメージを選んでください。Wi-Fi接続と十分なストレージ空き容量を推奨します。") }
    }
    private func refresh() async {
        guard !loading else { return }
        loading = true; error = nil
        defer { loading = false }
        do { entries = try await RemoteImages.catalog() }
        catch is CancellationError {}
        catch { self.error = "一覧を取得できませんでした。サーバー停止中の場合は後で再試行するか、フォルダから取り込んでください。\n\(error.localizedDescription)" }
    }
}
