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
    private var observer: Task<Void, Never>?
    private var cache: VMConfiguration.Cache = .balanced
    private var preparationFailed = false
    private var automaticallyAttempted = false
    // StikDebug may launch/attach directly without any button in this app.
    // Observe the actual process flag; opening a URL is not evidence of JIT.
    func observe(cache: VMConfiguration.Cache) {
        self.cache = cache
        guard observer == nil else { return }
        observer = Task { [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                if AEJITArenaReady() {
                    self.state = .ready
                    self.detail = "JIT準備完了"
                    self.observer = nil
                    return
                } else if UIApplication.shared.applicationState == .active &&
                            AEIsDebugged() && !self.preparationFailed && !self.automaticallyAttempted && self.state != .preparing {
                    self.automaticallyAttempted = true
                    self.enable(cache: self.cache, openStikDebug: false)
                }
                do { try await Task.sleep(for: .milliseconds(250)) } catch { return }
            }
        }
    }
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
            let mode = AEJITProtocolMode()
            guard mode >= 0 else { throw EmuError.jit("この端末のJIT保護方式を確認できません。") }
            let protocolRequired = mode == 1
            let url: URL? = openStikDebug ? try JITRequest.url(bundleID: Bundle.main.bundleIdentifier ?? "", pid: getpid(), requiresProtocol: protocolRequired) : nil
            if let url, !UIApplication.shared.canOpenURL(url) { throw EmuError.jit("StikDebugがインストールされていません。") }
            state = .preparing; detail = "対応するdebuggerの接続を待っています（120秒）"
            waiting = Task {
                let deadline = ContinuousClock.now.advanced(by: .seconds(120))
                while !Task.isCancelled && ContinuousClock.now < deadline {
                    if AEIsDebugged() && (!protocolRequired || AEIsDebuggerAttached()) {
                        let result = await Task.detached(priority: .userInitiated) { () -> String? in
                            var message = [CChar](repeating: 0, count: 512)
                            let capacity = message.count
                            let bytes = Int(AERecommendedCacheMiB(UInt32(cache.rawValue))) << 20
                            let ok = AEPrepareJITArena(bytes, protocolRequired, &message, capacity)
                            return ok ? nil : message.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
                        }.value
                        if let result { self.preparationFailed = true; self.state = .failed; self.detail = result }
                        else { self.state = .ready; self.detail = "JIT準備完了" }
                        return
                    }
                    do { try await Task.sleep(for: .milliseconds(200)) } catch { return }
                }
                if !Task.isCancelled { self.state = .failed; self.detail = "JIT接続がタイムアウトしました。" }
            }
            if let url {
                UIApplication.shared.open(url) { accepted in
                    if !accepted { Task { @MainActor in self.waiting?.cancel(); self.state = .failed; self.detail = "StikDebugを開けませんでした。" } }
                }
            }
        } catch { state = .failed; detail = error.localizedDescription }
    }
}
