import SwiftUI

struct DiagnosticsView: View {
    @ObservedObject var model: LibraryModel
    @ObservedObject var jit: JITCoordinator
    private var report: String {
        """
        AndroidEmu development build
        Host: \(ProcessInfo.processInfo.operatingSystemVersionString)
        Profile: \(model.manifest?.profile ?? "未選択")
        Profile name: \(model.selectedProfile?.name ?? "未選択")
        Guest RAM requested: \(model.configuration.ram.rawValue) MiB
        Guest RAM after board reservation: \(model.configuration.ram.guestMiB) MiB
        HIGHMEM kernel requested: \(model.configuration.ram.needsHighmemKernel)
        vCPU: \(model.configuration.cpuCount)
        TCG cache requested: \(model.configuration.cache.rawValue) MiB
        JIT: \(jit.state.rawValue)
        TXM: \(jit.txm.label); SPTM: \(jit.sptm.label)
        get-task-allow: \(jit.entitlement)
        Increased Memory Limit: \(AEHasIncreasedMemoryLimit())
        Available process memory: \(AEAvailableMemory() >> 20) MiB
        JIT usable arena bytes: \(AEJITArenaSize())
        Imported files: \(model.manifest?.files.count ?? 0)
        QEMU/Goldfish framework present: \(AEVMController().engineAvailable)
        Runtime API: embedded host ABI 1; one VM per process
        Guest boot / Metal / audio / network / APK: not device-validated
        """
    }
    var body: some View {
        List {
            Section("診断情報") { Text(report).font(.system(.footnote, design: .monospaced)).textSelection(.enabled) }
            Section { ShareLink("診断情報を共有", item: report) }
            Section { Text("イメージ・APK・ペアリング情報・端末固有IDは診断情報に含めません。自動送信は行いません。") }
        }.navigationTitle("Diagnostics")
    }
}
