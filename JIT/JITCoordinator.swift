import Foundation
import UIKit

@MainActor final class JITCoordinator: ObservableObject {
    enum State: String { case notReady = "Not Ready", preparing = "Preparing", ready = "Ready", failed = "Failed" }
    @Published private(set) var state: State = .notReady
    @Published private(set) var txm = FeaturePresence.unknown
    @Published private(set) var sptm = FeaturePresence.unknown
    @Published private(set) var entitlement = false
    @Published private(set) var detail = "JIT未準備"
    private var waiting: Task<Void, Never>?
    func refresh() {
        txm = FeaturePresence(rawValue: Int(AETXMPresence())) ?? .unknown
        sptm = FeaturePresence(rawValue: Int(AESPTMPresence())) ?? .unknown
        entitlement = AEHasGetTaskAllow()
    }
    func enable(cache: VMConfiguration.Cache, openStikDebug: Bool) {
        guard state != .preparing, state != .ready else { return }
        refresh()
        do {
            guard entitlement else { throw EmuError.jit("get-task-allowがありません。対応する署名方法で再インストールしてください。") }
            let url = try JITRequest.url(bundleID: Bundle.main.bundleIdentifier ?? "", pid: getpid(), txm: txm, sptm: sptm)
            if openStikDebug && !UIApplication.shared.canOpenURL(url) { throw EmuError.jit("StikDebugがインストールされていません。") }
            state = .preparing; detail = "対応するdebuggerの接続を待っています（120秒）"
            let protocolRequired = txm == .present || sptm == .present
            waiting = Task {
                let deadline = ContinuousClock.now.advanced(by: .seconds(120))
                while !Task.isCancelled && ContinuousClock.now < deadline {
                    if AEIsDebugged() && (!protocolRequired || AEIsDebuggerAttached()) {
                        let result = await Task.detached(priority: .userInitiated) { () -> String? in
                            var message = [CChar](repeating: 0, count: 512)
                            let capacity = message.count
                            let ok = AEPrepareJITArena(cache.rawValue << 20, protocolRequired, &message, capacity)
                            return ok ? nil : message.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
                        }.value
                        if let result { self.state = .failed; self.detail = result }
                        else { self.state = .ready; self.detail = "RX/RW領域準備・detach・実行テスト完了。TCG接続は別途必要です。" }
                        return
                    }
                    do { try await Task.sleep(for: .milliseconds(200)) } catch { return }
                }
                if !Task.isCancelled { self.state = .failed; self.detail = "JIT接続がタイムアウトしました。" }
            }
            if openStikDebug {
                UIApplication.shared.open(url) { accepted in
                    if !accepted { Task { @MainActor in self.waiting?.cancel(); self.state = .failed; self.detail = "StikDebugを開けませんでした。" } }
                }
            }
        } catch { state = .failed; detail = error.localizedDescription }
    }
}
