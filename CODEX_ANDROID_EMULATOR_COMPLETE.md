# Codex指示書：iOS向け高性能Android 5.1.1フルシステムエミュレータ

> この文書をプロジェクトの最上位仕様として扱うこと。Codexは本書の要件を満たす実装を直接進めること。
> 仕様に明記されていない細部は、性能・保守性・Android互換性・iOS 26+実機動作を優先して合理的に決定すること。
> 主要機能をダミー実装、TODO、空のスタブで済ませないこと。ビルドを常に可能な状態に保つこと。

---

## 1. プロジェクトの目的

iPhone/iPad上でAndroid OSそのものをフルシステムエミュレーションする、Android専用の高性能エミュレータを作成する。

これはAndroid API互換レイヤーではない。Android/Linux kernel、Android Framework、ART/Dalvik、SurfaceFlinger、SystemUI、Launcher、Androidアプリを含むゲストOSをQEMU TCGで実行する。

汎用VMアプリではなく、Android 5.1.1 / API 22だけに徹底的に最適化した専用エミュレータとする。

ゲストOSはAndroid 5.1.1に固定する。他のAndroidバージョンへの互換性・自動判定・汎用VM化は目的に含めない。

Apple Hypervisor / Virtualization.frameworkを実行エンジンとして使用しない。CPUはQEMU TCGによるエミュレーション/JITで実行する。

アプリはApp Store公開を目的としない。サイドロード利用を前提とし、App Store審査で許可されないJIT、get-task-allow、外部JIT有効化ツールとの連携、必要に応じた非公開API等も、技術的に有効なら利用可能とする。ただし不要な非公開API依存は増やさず、使用箇所を隔離すること。

---

## 2. 絶対要件

- Deployment Target: iOS 26.0+
- 実機arm64のみを正式ターゲットとする
- Androidイメージをアプリ本体・Gitリポジトリ・GitHub Actions Artifactへ同梱しない
- ユーザーがAndroid 5.1.1用の所定イメージをFiles/Document Pickerから選択・取り込みする
- イメージ選択式である理由はIPAサイズを小さく保つためであり、ユーザーごとに異なるゲスト構成を想定しない
- 全ユーザーが同一仕様のAndroid 5.1.1イメージを使用する前提で設計する
- Android 5.1.1 / API 22のみを正式ゲストとする
- Google Play Store / Google Play Services / GMSをアプリ側から追加しない
- CPU実行はQEMU TCG JIT
- 古いAltJIT系や旧式JIT互換レイヤーは実装しない
- iOS 26+のTXM/SPTM環境に対応したmodern JIT protocolを実装する
- StikDebugは外部アプリとして利用可能にする
- StikDebugを本アプリへビルトインする必要はない
- JITが利用可能になるまでVMのCPU実行を開始しない
- Release版では低速なTCI/interpreter fallbackを通常動作として用意しない
- タッチをマウスへ変換しない
- Androidから本物のタッチスクリーンとして認識させる
- マルチタッチ対応
- Back / Home / Recents / Power / Volume Up / Volume Downをエミュレートする
- 旧来のAndroidアプリ互換用としてMenu / Searchキーをオプションで提供可能にする
- SPICE/QXL/VNCを標準表示経路に使用しない
- Metalで直接表示する
- 画面コピー回数と入力遅延を最小化する
- 不要な仮想ハードウェアを起動しない
- GitHub Actionsのみで未署名IPAを生成できる
- 生成した未署名IPAをWorkflow Artifactへアップロードする
- Apple Developer証明書やProvisioning ProfileをCIに要求しない
- サイドロード時にJIT用entitlementを付けられるよう、必要なEntitlements.plistもArtifactへ含める
- コンパイル警告を放置しない。ただし第三者ライブラリ内部の既知警告は自作コードと分離して扱う
- AndroidイメージやユーザーのAPKを外部サーバへ送信しない

---

## 3. 参考・流用する既存プロジェクト

ゼロから再発明しない。既存OSSから合法的に最大限流用する。

### 3.1 UTM / utmapp/UTM

特に参考・移植する対象:

- iOSアプリ内でQEMUを別プロセスではなくライブラリとして動かす構成
- QEMUのDarwin/iOS対応
- QEMU起動・停止・状態管理
- QMP/QAPIブリッジ
- iOS向けQEMU dependency build scripts
- arm64 iOS向けsysroot生成
- QEMUをshared libraryとしてリンクする方法
- iOS上のメモリ管理/JIT関連パッチ
- Metal表示に利用できる既存コード
- iOS固有のQEMU制約への対処
- GitHub Actions上で依存関係をキャッシュしてiOSビルドする方法
- unsigned archive/IPA packagingの考え方

ただしUTM UI全体や汎用VM設定システムを丸ごと持ち込まない。
本プロジェクトはAndroid専用なので、Windows/x86/PPC/RISC-V/USB/SPICE/VNC等の不要部分は排除する。

### 3.2 utmapp/qemu

QEMUのベース候補として最優先する。

理由:
- iOS host supportがすでに存在
- QEMUをiOS上でライブラリ利用するための変更がある
- UTMで実運用されている
- TCG/JITをiOSで動かすための知見が蓄積されている

必要ならupstream QEMUとの差分を追い、Android固有デバイスだけ別パッチとして管理する。

### 3.3 AOSP Android Emulator / platform/external/qemu

Android 5.1.1 / API 22 / ARMv7のstock Android Emulator imageを動かすための最重要参照元。

本プロジェクトは全ユーザーが同一仕様のAndroid 5.1.1 ARMv7イメージを使う前提とし、Goldfish互換だけを実装対象とする。複数世代のAndroid Emulator machineを汎用的に扱う必要はない。

移植・参考対象:

- Goldfish machine
- goldfish framebuffer
- goldfish events/input
- goldfish audio
- goldfish battery
- goldfish pipe / qemud
- Android boot properties
- Android kernel command line生成
- ADB bridge
- Android 5.1.1 ARM向けblock/network構成
- ARM board初期化
- emulator-specific device tree
- `androidboot.hardware=goldfish`
- Android SDK API 22 ARM imageの起動処理

基準ゲストはAndroid 5.1.1 / API 22 / `default` / `armeabi-v7a`系のAOSP emulator imageを想定する。KernelはAndroid 5.1.1世代のGoldfish ARMv7系を基準にする。

最新QEMUへ移植する際はAndroid固有コードを `ThirdParty/AndroidQemuCompat/` などへ隔離し、upstream QEMUの変更を最小化する。

### 3.4 StikDebug/StikJIT

iOS 26+ JIT対応の仕様参照元。

特に:

- universal JIT protocol
- TXM/SPTM detection
- executable region preparation
- debugger attach中に実行領域を準備する方式
- JIT26Detach
- StikDebug URL scheme integration
- get-task-allow要件
- JIT ready判定
- iOS 27のA13/A14/M1 TXM判定修正の考え方

本アプリへStikDebug本体を埋め込まない。
本アプリ側はmodern JIT protocolを正しく実装し、外部StikDebugからJITを有効化できる状態にする。

### 3.5 ANGLE / Android emulator graphics code

Android 5.1.1のOpenGL ESアクセラレーションのために利用を検討・実装する。

理想経路:

Android GLES
→ Goldfish guest transport
→ goldfish pipe / emulator GL command stream
→ host renderer
→ ANGLE
→ Metal

SPICEをGPU高速化のために挟まない。

### 3.6 AOSP libsparse

Android sparse imageが渡された場合にraw imageへ変換するために利用する。
可能ならimport時に一度だけ変換し、VM実行中の変換コストをゼロにする。

---

## 4. ライセンス方針

「既存コードをパクる」は、ライセンス上許可された再利用・移植を意味する。

必ず以下を行う。

- 元プロジェクトのLICENSEを確認する
- コピーしたファイルのcopyright headerを消さない
- 改変元と改変内容が分かるようにする
- `THIRD_PARTY_NOTICES.md` を作る
- `ThirdParty/README.md` に各依存元・commit hash・license・利用目的を記録する
- Git submoduleまたは固定commitで依存バージョンをpinする
- moving `main` へ無条件追従しない
- QEMU由来コードのGPL義務を無視しない
- UTM本体のApache-2.0部分とQEMU/GPL/LGPL部分を混同しない
- IPAを配布する場合、GPL等で要求される対応ソース提供条件を満たせる構成にする
- Android/Linux kernel imageを本プロジェクトが配布する場合は、そのライセンス義務を別途満たす
- Google proprietary image/GMSを再配布しない

ライセンス上不明なコードは直接コピーせず、仕様/挙動だけ参考にして独自実装する。

---

## 5. リポジトリ構成

```text
/
├── App/
├── Core/
├── ImageKit/
├── JIT/
├── QEMUBridge/
├── Display/
├── Input/
├── Audio/
├── Network/
├── ADB/
├── Performance/
├── ThirdParty/
│   ├── qemu/
│   ├── AndroidQemuCompat/
│   └── StikJITProtocol/
├── scripts/
│   ├── bootstrap.sh
│   ├── build_dependencies.sh
│   ├── build_ios.sh
│   ├── package_unsigned_ipa.sh
│   └── verify_artifact.sh
├── Tests/
├── AndroidEmu.xcodeproj/
├── AndroidEmu.entitlements
├── THIRD_PARTY_NOTICES.md
├── LICENSE
├── README.md
└── .github/workflows/build-ios.yml
```

責務分離を維持し、巨大な単一ファイルに実装を集中させない。

---

## 6. アプリ全体アーキテクチャ

```text
┌─────────────────────────────────────┐
│               iOS App               │
│ SwiftUI settings/library            │
│ UIKit touch/runtime view            │
│ Metal renderer                      │
│ AVAudioEngine                       │
│ UIDocumentPicker                    │
│                                     │
│ ┌─────────────┐  ┌────────────────┐ │
│ │ JIT Manager │  │ Image Manager  │ │
│ └──────┬──────┘  └───────┬────────┘ │
│        │                 │           │
│ ┌──────▼─────────────────▼─────────┐ │
│ │           QEMU Bridge           │ │
│ │     QEMU TCG / Android board    │ │
│ └──────┬─────────┬─────────┬──────┘ │
│        │         │         │         │
│    Display     Input     Audio/Net   │
└────────┼─────────┼─────────┼─────────┘
         │         │         │
┌────────▼─────────▼─────────▼─────────┐
│             Android Guest            │
│ Linux kernel                         │
│ Android 5.1.1 / API 22              │
│ SurfaceFlinger / ART / Framework     │
│ Launcher / APKs                      │
└──────────────────────────────────────┘
```

SwiftUIはライブラリ画面・設定画面等に使用してよい。
実行画面は入力遅延と描画制御を優先し、UIKit + MetalKitを中心にする。

QEMUはiOSで子プロセスとして起動する前提にしない。
UTMと同様、アプリ内ライブラリとして実行する。

QEMUのグローバル状態上、1プロセス内で複数回の完全初期化が安全でない場合は、1アプリプロセスにつき1VM lifecycleを明示的制約として扱う。無理に危険な再初期化を行わない。

---

## 7. Androidイメージの取り込み

Androidイメージは絶対にアプリへ同梱しない。これは複数のAndroidバージョンや自由なVM構成を選べるようにするためではなく、巨大なsystem image等をIPAから分離してアプリ配布サイズを小さくするためである。

**全ユーザーが同一仕様のAndroid 5.1.1イメージを使用する前提とする。**

基準イメージ仕様:

```text
Android       5.1.1 / API 22
Variant       AOSP / default / Google Play Servicesなし
Architecture  ARMv7 / armeabi-v7a
Machine       Goldfish compatible
```

ユーザーは初回起動時またはSettingsから、所定のAndroid 5.1.1 image bundleをFiles/Document Pickerで選択する。

想定bundle:

```text
kernel-qemu / kernel
ramdisk.img
system.img
userdata.img
cache.img                 optional
hardware-properties.ini   optional
source.properties         optional
package.xml               optional
```

アプリは「何でも読み込める汎用importer」にしない。選択されたファイルが期待するAndroid 5.1.1 ARMv7/Goldfishイメージと一致するかを検証し、明らかに別API level・別architecture・別machine向けならエラーにする。

複数VM、複数Androidバージョン、Ranchu、ARM64、x86、カスタムmachineの選択UIは不要。

Document Pickerで取得したsecurity-scoped URLをVM実行中に直接参照し続けず、原則としてApplication Support以下へコピーする。

```text
Application Support/
└── Android51/
    ├── image.json
    ├── kernel
    ├── ramdisk.img
    ├── system.img
    ├── userdata.img
    └── cache.img
```

`system.img`は可能ならread-only、`userdata.img`/`cache.img`はread-write。

ZIP importを許可する場合はzip-slip/path traversalを防止する。Android sparse imageを検出したらlibsparse相当でrawへ変換し、元ファイルを破壊しない。イメージの実データをiCloud等へ自動同期しない。

将来、自前で軽量化したAndroid 5.1.1イメージへ置き換える場合も、この固定イメージ契約を維持し、アプリ側の汎用VM化は行わない。

---

## 8. 固定VM Profile

VM Profileは1種類だけ持つ。

### Android 5.1.1 ARMv7 Goldfish

- Android 5.1.1 / API 22
- ARMv7-A / armeabi-v7a
- Goldfish board
- Goldfish events/input
- Goldfish framebuffer
- Goldfish audio
- Goldfish pipe/qemud
- Android 5.1.1世代のGoldfish kernel
- stock Android Emulator API 22 ARM image互換
- Google Play Services/GMSは本プロジェクトから追加しない

ユーザーへmachine、architecture、Android version、kernel args等を通常設定として選ばせない。デバッグ用途の高度な上書きが必要な場合も、開発用Diagnosticsへ隔離し、製品UIを汎用VM設定画面にしない。

---

## 9. QEMU構成

必要なsystem targetだけをビルドする。

- qemu-system-arm

不要なx86/PPC/RISC-V/SPARC/MIPS/s390x/user-mode/VNC/SPICE/QXL/USB redirection/migration/record-replay等は除外する。

CPUはTCG JIT必須。
1 vCPU / 2 vCPUを選択可能にし、標準は2 vCPU。4 vCPU以上は実測メリットが確認できるまで標準にしない。
MTTCGを有効化する。

Guest RAM preset:

```text
Low       512 MiB
Balanced  640 MiB
High      768 MiB
Maximum   1024 MiB
```

TCG code cache:

```text
128 MiB
192 MiB default
256 MiB performance
```

JITコード領域はVM開始前に可能な限りすべて準備し、VM実行中の未準備RX領域追加を避ける。
TB flush回数を計測し、不要なflushを避ける。

Releaseではdebug assertions/verbose tracingを抑え、必要ならLTOを検証する。hot pathへSwift/ObjC bridgeを挟みすぎない。

---

## 10. iOS 26+ Modern JIT

古いJIT方式は切り捨てる。

### 必須

サイドロード後のアプリにJIT取得に必要なentitlementが付与される前提とする。
`AndroidEmu.entitlements`を用意し、少なくともmodern StikDebug/StikJIT対象アプリで必要な`get-task-allow`を考慮する。

Unsigned IPAには署名entitlementそのものは埋め込まれないため、Artifactには:

- unsigned IPA
- AndroidEmu.entitlements
- signing-notes.txt

を含める。

### universal protocol

StikJITの現行Integration Guideを仕様の正とする。

TXM/SPTMが存在する環境では単純なdebugger attach/detachだけをJIT成功とみなさない。

JIT allocatorは:

1. executable regionを確保
2. debugger接続中にuniversal protocolでregionをprepare
3. 必要なすべてのregionのprepare完了を確認
4. detach protocolを完了
5. 実際にJITコードが実行可能なことを検証
6. その後にだけQEMU TCGを開始

すること。

`JIT26PrepareRegion` / `JIT26Detach` 等はStikJITの現行コード/仕様と一致させる。
古いブログ記事や過去のAltJITコードを基準にしない。

### TXM/SPTM判定

OS versionだけで分岐しない。

```text
ModernJITEnvironment
├── TXM present?
├── SPTM present?
├── get-task-allow available?
├── external debugger attached?
├── all JIT regions prepared?
└── executable self-test passed?
```

iOS 27のA13/A14/M1でTXM検出方法が変わった実例を考慮し、feature detectionを優先する。

### StikDebug連携

StikDebugは外部アプリ。

```text
JIT
Status: Not Ready / Preparing / Ready / Failed

[Enable with StikDebug]
[Wait for Compatible Debugger]
```

`LSApplicationQueriesSchemes`へ`stikdebug`を追加する。

StikDebug URL schemeを使う場合、bundle identifier/current PID/TXM-SPTM環境で必要なscript情報を現行StikJIT Integration Guideに従って渡す。
script protocolは`universal`固定。ユーザーにlegacy/universalを選ばせない。

URLを開けたことをJIT成功扱いしない。アプリ側のJIT readiness判定が成功するまでVM Startを許可しない。

---

## 11. Android boot

Android Emulatorの既存ロジックを可能な限り流用する。

```text
-kernel <kernel>
-initrd <ramdisk>
-drive / block backend for system
-drive / block backend for userdata
-append <kernel cmdline>
```

固定Goldfish profileとして`androidboot.hardware=goldfish`等、Android 5.1.1 API 22 ARM imageが期待するboot parameterを設定する。
Android Emulatorが使用するboot-properties/qemud/goldfish-pipe依存を確認し、stock SDK imageが期待するホスト側機能を実装する。

SELinuxを性能目的だけで無条件permissiveにしない。

---

## 12. 仮想ハードウェア

必須:

- CPU/MMU/interrupt controller/timer/RTC/RAM
- serial/debug console
- system/userdata storage
- network
- display
- touchscreen/input
- Android hardware keys
- audio output
- minimal battery/power state
- goldfish pipe/qemud等、stock imageが起動に必要とするAndroid Emulator device

標準で無効:

- GSM modem/RIL/SIM
- camera
- GPS/GNSS
- Bluetooth
- NFC
- fingerprint
- USB passthrough
- accelerometer/gyro/magnetometer/proximity/light
- telephony audio
- webcam/printer/removable optical media

Android Framework API自体を消すのではなく、「hardware feature is absent」として見せる。
batteryは軽量stubでcharging/100%/AC powered等を返せるようにする。

---

## 13. Display / Metal

標準経路でQXL/SPICE/VNCを使用しない。

```text
Guest framebuffer / Android Emulator display device
→ QEMU display callback
→ dirty region tracking
→ host frame queue
→ Metal texture
→ MTKView
```

要件:

- MTKView
- 最大60Hzを基本
- frame queueは低遅延優先
- 古いframeが溜まれば最新優先でdrop
- CPUで毎フレーム全画面scaleしない
- scaling/orientation/color conversionは可能な限りMetal shader
- dirty rectangle/page利用
- 変更がない時にfull copyしない
- render threadをSwiftUI main threadで塞がない

解像度preset:

```text
480×854
540×960 default
720×1280
```

### GPU acceleration

固定Android 5.1.1 ARM imageのemulator GLES stackを可能な限り利用する。

```text
Guest OpenGL ES
→ emulator guest driver
→ goldfish pipe / command stream
→ iOS host renderer
→ ANGLE
→ Metal
```

host renderer未対応のguestではsoftware renderer + fast framebufferでも起動可能にする。
ただしSPICEを標準GPU経路へ戻さない。

---

## 14. Native Multi-Touch

指をマウスカーソルとして動かすUIは禁止。

iOS側はUITouchごとにstable touch ID/x/y/phase/timestamp/optional forceを取得し、表示領域を考慮してguest座標へ変換する。

Android 5.1.1 Goldfish emulatorのevents device/input injectionを移植し、guestから直接タッチスクリーンとして認識させる。

Androidからtouchscreen/direct/multi-touchとして認識させる。
Linux Multi-Touch Protocol B相当を優先し、最低10 touch slotまで設計可能にする。

タップ、長押し、ドラッグ、フリック、2本指、ピンチ、ゲームの同時押しをiOS側でgestureへ解釈せず、raw touchとしてAndroidへ送る。

---

## 15. Androidボタン

runtime UIで:

- Back
- Home
- Recents
- Power
- Volume Up
- Volume Down
- optional Menu
- optional Search

を提供し、本物のAndroid/Linux input key eventとして送る。
キーコードを独自に推測せず、AOSP keylayout/Linux input mappingを参照する。
長押し/リピートも正しいevent sequenceを生成する。

---

## 16. Keyboard

Androidの画面内IMEはAndroid自身に描画させる。
iOSキーボードをAndroid IMEの代わりに強制表示しない。

外付けkeyboardはUIKey/GCKeyboard等からAndroid keycodeへ変換し、guest input deviceへ送る。
矢印/Enter/Escape/Tab/modifier等もマップする。

---

## 17. Audio

固定Android 5.1.1 image互換を優先し、Goldfishが期待するaudio deviceを移植する。

```text
QEMU audio samples
→ ring buffer
→ AVAudioEngine
→ iOS audio output
```

16-bit PCM/stereo/44.1・48kHz対応。VM threadをaudio I/Oでblockしない。AVAudioSession interruption対応。microphoneは必須ではない。

---

## 18. Network

モデム/Wi-Fiチップを忠実にエミュレートしない。

- QEMU user-mode networking/slirp系
- NAT
- DNS
- IPv4
- 可能ならIPv6
- host listen portなしがdefault

Android 5.1.1 Goldfish emulator imageが期待するNIC構成へ固定する。GSM/RILは無効。

---

## 19. ADB / APKインストール

iOS上で外部adbプロセスを起動する前提にしない。
Android EmulatorのADB bridgeまたはin-process ADB clientを利用する。

```text
Import APK
→ FilesからAPK選択
→ VM sandboxへ一時コピー
→ embedded ADB transport
→ /data/local/tmp/
→ pm install / pm install -r
→ 結果表示
```

installed packages一覧/uninstall/reinstall/logcat/Advanced shell/boot-completed検出を可能にする。
ADB transportをLANへdefault公開しない。

---

## 20. UI

Library画面、Add Android、VM Settings、Runtime、JIT画面、Diagnosticsを持つ。

RuntimeはAndroid画面を最大化し、下部にBack/Home/Recents、端にPower/Volume overlayを配置する。
UI controlsは自動的に薄く/隠せる。マウスカーソルは表示しない。

JIT画面にはReady/Not Ready/Preparing/Failed、TXM/SPTM、実行領域状態、TCG cacheを表示する。秘密情報はlogへ出さない。

---

## 21. Performance最優先設計

- Androidに不要なmachine/device/backendをコンパイルしない
- TCG/framebuffer/touch/audio/block I/Oのhot pathでSwift allocationを避ける
- guest frame arrivalとiOS VSyncを分離
- latest-frame-wins
- frame queueを深くしない
- input-to-photon latencyを計測
- full-frame memcpy回数を可視化
- system read-only、userdata async I/O
- iCloud同期対象外
- Release logはerror/warning中心
- Debug logはbounded ring buffer

hostが3GB RAM端末でも動作を狙う。

---

## 22. Performance Overlay

表示可能にする:

- guest FPS
- presented FPS
- frame drops
- frame copy time
- Metal render time
- input latency estimate
- vCPU count
- TCG code cache usage
- TB flush count
- app resident memory
- guest RAM
- audio underruns
- thermal state

通常利用時は非表示。
`os_signpost`でframe copy/render/input/QEMU loop/JIT region/block I/OをInstrumentsから追跡可能にする。

---

## 23. Error Handling

意味のある個別エラーを表示する。

- JIT not enabled
- get-task-allow missing
- StikDebug not installed
- JIT region preparation failed
- unsupported architecture
- missing kernel/ramdisk
- sparse conversion failed
- unsupported profile
- guest kernel panic
- boot timeout
- storage error
- memory pressure
- Metal error
- ADB install failure

`QEMU failed`だけで終わらせない。
serial/QEMU log末尾をエラー画面からコピー可能にする。

---

## 24. Security / Privacy

- VM image/APKはローカルsandbox
- telemetry/analyticsなし
- crash log自動送信なし
- pairing/JITデータをlogしない
- ADB LAN公開なしがdefault
- ZIP path traversal防止
- 展開サイズ上限
- symlink escape防止
- parser overflowチェック
- guest由来データをtrusted扱いしない

---

## 25. 言語ごとの責務

Swift:
- UI/config/document picker/lifecycle/settings/error/JIT high-level state/performance UI

Objective-C++:
- Swift↔QEMU bridge/C++ API/Metal-QEMU low-level integration

C/C++:
- QEMU/TCG/Android virtual devices/audio ring buffer/display callback/hot input/libsparse/low-level JIT allocator

高頻度処理をMainActorへ無理に通さない。

---

## 26. Concurrency

- MainActor: UIのみ
- QEMU: dedicated thread
- vCPU: MTTCG
- Metal:専用render scheduling
- Audio:real-time callbackでallocation/lockを避ける
- import/conversion:background
- ADB:background
- logs:bounded queue

VM threadをMainActorで動かさない。
touchは最小データへ変換後すぐlow-lock queueへ渡す。

---

## 27. Tests

Swift unit tests:

- VMConfiguration encode/decode
- profile detection
- image validation
- touch coordinate/orientation
- key mapping
- JIT state machine
- StikDebug URL construction
- frame queue drop policy
- APK command escaping

Native tests:

- ring buffer
- sparse image detection
- QEMU argv builder
- Android boot args
- fixed Android 5.1.1 profile validation
- JIT region bookkeeping

CIではAndroid imageを同梱しないためcompile/unit test中心とする。

---

## 28. GitHub Actions

GitHub Actionsで未署名IPAを自動生成し、Workflow Artifactへアップロードできるようにする。

現時点ではworkflowの具体的YAMLやshell scriptの完成形を本仕様書に固定しない。Codexは実装時点のGitHub Actions/macOS/Xcode環境と、採用したQEMU依存ビルド方式に合わせて最も単純で再現性の高いworkflowを作成すること。

必須要件:

- macOS runnerを使用する
- iPhoneOS / arm64向けRelease buildを行う
- CI上でApple Developer certificateを要求しない
- Provisioning Profileを要求しない
- code signingを無効にした`.app`を生成する
- `.app`を`Payload/`へ配置しunsigned `.ipa`としてpackageする
- unsigned IPAをGitHub Actions Artifactへアップロードする
- 必要なJIT entitlement情報/署名時注意事項をIPAとは別ファイルでもArtifactへ含める
- dSYM等のデバッグシンボルを必要に応じて別Artifactへ保存する
- QEMU/ネイティブ依存関係のビルド結果を安全にcache可能な構成にする
- Android system image/kernel/ramdisk/userdataをCI Artifactへ含めない
- AndroidイメージをCIで勝手にダウンロードして再配布しない
- workflow_dispatchおよび通常のpushでビルド可能にする
- PRでは少なくともcompile/testを実行できるようにする
- build failure時に根本原因を追えるログを残す

具体的なrunner label、Xcode version、Actions version、Homebrew package一覧等は、実装時点の現行環境に合わせる。古いサンプルworkflowをそのまま固定しない。

---

## 29. Entitlements

`AndroidEmu.entitlements`をrepoへ置き、現行JIT方式に必要な最小限だけを記載する。
少なくとも`get-task-allow`を考慮する。

署名後に必要entitlementが無ければDiagnosticsで検出して明示的にエラーを出す。
「あるはず」と仮定しない。

---

## 30. README必須内容

- Android image非同梱
- ユーザー自身が合法的に利用可能なimageを用意
- supported guest: Android 5.1.1 / API 22 only
- expected image: fixed ARMv7/Goldfish-compatible Android 5.1.1 image
- iOS 26+ only
- JIT required
- StikDebugは別アプリ
- App Store向けではない
- unsigned IPA取得方法
- image import方法
- JIT enable方法
- supported Android 5.1.1 image file layout
- license/GPL source情報
- troubleshooting/log export

Google proprietary system imageやGoogle Play Servicesを本プロジェクトが提供するかのような説明は禁止。

---

## 31. Codex作業ルール

1. 本書を最上位仕様とする。
2. 軽微な不明点は質問せず、reference projectを調査して合理的に決める。
3. 既存実装がある機能を再発明しない。
4. UTM / utmapp/qemu / Android Emulator / StikJITを積極的に調査する。
5. 再利用前にライセンス確認。
6. コピー元を`THIRD_PARTY_NOTICES.md`へ記録。
7. external dependencyを固定commitへpin。
8. 巨大な一枚ファイルへ集中させない。
9. hot pathでは性能優先。
10. warningを新規追加しない。
11. third-party warningとproject warningを区別。
12. 変更後に該当targetをbuild/test。
13. CIを常に壊さない。
14. build failureは最初の根本原因から直す。
15. Android imageをコミットしない。
16. Google proprietary imageをテスト用でもrepoへ追加しない。
17. certificate/provisioning profile/secretsをrepoへ入れない。
18. App Store制限だけを理由に性能を落とさない。
19. Apple Hypervisorへの置換を提案しない。本プロジェクトはTCG emulator。
20. マウス操作を標準にしない。
21. SPICEを標準display pathへ導入しない。
22. JITなしfallbackを完成扱いしない。
23. iOS 26+ JITは現行StikJIT universal protocolを基準にする。
24. 古いiOS/JIT互換コードを増やさない。
25. performance regressionを測定可能にする。
26. temporary workaroundには理由を書く。
27. core機能をstub/TODOだけで完成扱いしない。
28. Build/Tests/IPA packagingを維持。
29. reference projectと推測が食い違う場合は実コード/現行仕様を優先。
30. 性能問題はプロファイルしてから修正し、思い込みで最適化しない。

---

## 32. 完成状態

- iOS 26+実機へサイドロード可能
- modern JITを外部StikDebug等で有効化可能
- TXM/SPTM対応
- ユーザーが所定のAndroid 5.1.1 imageを選択
- image非同梱（IPAサイズ削減目的）
- 全ユーザーが同一仕様のimageを使う前提
- Android 5.1.1 ARMを起動
- Launcherまで到達
- 直接タッチ/マルチタッチ
- マウスポインタなし
- Back/Home/Recents
- Power/Volume
- audio output
- network access
- FilesからAPK install
- Metal表示
- 60Hzを目標にした低遅延frame pacing
- QEMU TCG JIT
- 不要hardware無効
- Google servicesを本プロジェクトから追加しない
- diagnostics/performance overlay
- GitHub Actionsのみでunsigned IPA生成
- IPAをArtifactへupload
- source/third-party license整理
- UTMの単なるAndroid設定版ではなく、Android専用に削減・最適化

---

## 33. 最重要判断基準

複数案がある場合は以下の順で判断する。

1. Android 5.1.1の実用互換性
2. iOS実機での体感性能
3. input-to-display latency
4. CPU/TCG性能
5. RAM消費
6. 安定性
7. 保守性
8. UI
9. 汎用VMとしての拡張性

本プロジェクトは「何でも動かせるVM」を目指さない。

**Android 5.1.1をiPhone/iPad上で、可能な限りAndroidスマートフォンそのものの操作感で高速に使えることを最優先する。**
