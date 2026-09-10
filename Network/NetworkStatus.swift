import Foundation
import Network

@MainActor final class NetworkStatus: ObservableObject {
    @Published private(set) var description = "接続を確認中"
    private let monitor = NWPathMonitor()
    private let queue = DispatchQueue(label: "org.androidemu.network-path", qos: .utility)
    init() {
        monitor.pathUpdateHandler = { [weak self] path in
            let text: String
            if path.status != .satisfied { text = "ホストはオフライン" }
            else if path.usesInterfaceType(.wifi) { text = "Wi-Fi · ゲストはNAT接続" }
            else if path.usesInterfaceType(.cellular) { text = "モバイル回線 · ゲストはNAT接続" }
            else { text = "ホスト接続あり · ゲストはNAT接続" }
            Task { @MainActor [weak self] in self?.description = text }
        }
        monitor.start(queue: queue)
    }
    deinit { monitor.cancel() }
}
