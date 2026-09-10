# 実装・検証状況

仕様書は目標であり、動作確認済み機能の一覧ではありません。今回、iOSアプリからQEMUを呼ぶ経路、全画面Runtime、Network、Performance、ADB/APK操作を実装しました。**この環境にはXcode/iOS SDK・実機・Androidイメージがないため、iOSビルド成功とAndroid Launcher到達は未確認です。完全動作を確認した状態ではありません。**

| 領域 | 実装した内容 | 検証・制約 |
|---|---|---|
| iOS Runtime | 専用実行スレッドでQEMU shared libraryを起動、1プロセス1VM、全画面、ステータスバー／ホームインジケータ非表示、操作シート | Swift/ObjC++を含むXcodeビルドと実機UIは未検証 |
| CPU / JIT | universal protocolで準備したRX/RW aliasをTCGへ登録。iOSで別の未準備実行領域を確保しない | ホストで外部arenaの実使用とTCG実行を確認。TXM/SPTM実機は未検証。Goldfishは1 vCPU |
| Goldfish | PIC、bus、TTY、timer/RTC、NAND、可変寸法framebuffer、events、battery、audio、pipe、boot-properties、ADB | 実QEMUによるMMIO/DMAテスト成功。stock Android kernelとの互換性は未確認 |
| 描画 | QEMUからBGRA dirty updatesを直接受信。Metal専用queue、1フレームのみ進行、変更のない画面の再送抑制 | ホストで実ARMコードが書いた画素をcallbackで検証。iOS Metal描画は未検証 |
| 全画面 | 起動時の端末の画面比率に合わせて高さを決定。描画・guest ABS範囲・UIKit座標を共通寸法に設定 | 幅540、高さ480〜1600の偶数。実行中のホスト回転ではアスペクト比を保持して表示 |
| タッチ／キー | 10点Protocol B、BQL下でinput queueをdrain、Back/Home/Recents/Power/音量、押下時間の保持、キュー満杯時の解除リトライ | native queueとguest capabilitiesを検証。実guestのジェスチャー／キー操作は未確認 |
| 外付けキーボード | USB HID物理キーをLinux evdevへ変換。左右修飾キー、押下／解放、background時の解除 | 主要マッピングをnativeコンパイル時に検査。実キーボード未検証 |
| 音声 | GoldfishのPCMをAVAudioSourceNodeへ接続。render callbackは固定バッファ・allocation/lockなし。停止／再開ではリングを更新 | QEMU→hostの実PCM転送を検証。AVAudioSession、interruption、実音声は未検証 |
| Network | SMC91C111 + libslirp NAT、固定DHCP/DNS設定、公開port forwardingなし。NWPathMonitorでホスト接続を監視 | ホストでNIC/slirp初期化を確認。guest DHCP/DNS/インターネット疎通とiOSでは未検証 |
| ADB transport | Goldfish `qemud:adb` accept/start handshake、双方向の有界ring、backpressure、disconnect/reconnect | 合成ARMゲストとの双方向転送を実QEMU shared library上で検証。公開ADBソケットなし |
| ADB client / APK | classic CNXN/AUTH/OPEN/WRTE/OKAY/CLSE、Keychain RSA、sync SEND/DATA/DONE、pm install、shell/logcat、boot_completed判定 | AUTH/分割packet/shell/sync/キャンセル/RSA公開鍵形式のnative tests成功。実adbd認証・pm install・iOS Keychainは未検証 |
| Performance | guest更新数、GPU表示数、転送量、実メモリ使用量、TCG使用量／容量、TB flush、PCM欠損、thermal state | TCG容量を実エンジンで検証。iOSのFPS・input-to-photon・温度／消費電力は未測定 |
| イメージ | Filesから取り込み、sparse変換、上限／CRC／symlink検査、SHA記録、system read-only、userdata/cache書き込み | portable parser tests成功。iOS Files importer未検証 |
| ビルド／IPA | 固定UTMの必要依存だけをiOSへcross-build、QEMU frameworkと依存closureをIPAへembed、対応ソースを同梱Artifactとして生成 | スクリプト生成／構文／packagerの検査成功。macOS cross-build、GitHub Actions実行、実IPA成果物は未確認 |
| GPU acceleration | `qemu.gles=0`でguest software renderer + Metal framebuffer経路を選択 | ANGLE・emulator GLES command decoderは未実装。仕様で許容されたsoftware経路のguest起動も未確認 |

## この環境で実行した検証

- GCC 13でQEMU 10 + Goldfish shared libraryを`--enable-werror`でビルド。
- 実QEMUのGoldfish結合テスト8件成功。NAND読み書きと永続化、system保護、batch入力保存、MMIO再入拒否、timer/VSYNC/音声IRQ、input capabilities、pipe framingを確認。
- アプリと同じ埋め込みABIの結合テスト1件成功。外部RW/RX arena、ARM命令、540×1170 framebufferの画素、PCM、serial、input callback、ADB双方向転送、一時停止／再開／停止、再起動拒否、TCG容量を確認。
- Clang 18 / C++20 / ASan・UBSanでnative coreとADBの2テスト実行ファイルが成功。ADBでは分割read/write、AUTH署名／公開鍵の順序、shell、17,001-byte sync転送、キャンセル、RSA Montgomery形式を検証。
- Python toolingテスト13件成功。framework依存closure、macOS／他architecture／host依存の拒否、IPA内のarm64エンジンの必須検査、host launcher、project再生成を確認。
- shell/Python構文、固定UTMからの最小cross-build script生成を確認。

QEMUテストは合成したARM命令とデータだけを使用します。Androidの画面・boot_completed・Launcherを模擬して成功扱いにはしていません。

## 残る検証と開発

1. Xcode 26のMacまたはGitHub Actionsで、依存cross-buildからIPA生成まで実行し、iOS固有のビルドエラーがあれば解消する。
2. 実機へ全frameworkを含めて再署名・サイドロードし、modern JITの領域準備とQEMUの使用を検証する。
3. ユーザー提供のAPI 22 stock kernel/ramdisk/systemでbootログを取得し、必要なGoldfish/qemud互換性修正を進める。serial qemud fallbackや未対応pipeサービスは残っている。
4. Launcher、touch、音声、guestネットワーク、ADB許可画面、APKインストール、background／復帰を実測する。
5. 必要な性能に応じてANGLE/GLESを追加し、実機計測に基づいて調整する。現段階で高FPSやゲーム互換性を保証する計測結果はない。
