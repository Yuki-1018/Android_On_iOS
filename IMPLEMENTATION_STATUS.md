# 実装・検証状況

仕様書は目標であり、動作確認済み機能の一覧ではありません。今回、iOSアプリからQEMUを呼ぶ経路、全画面Runtime、Network、Performance、ADB/APK操作を実装しました。**ユーザーからGitHub Actions成功と実機でのAndroid起動ロゴ到達が報告されています。この環境にはXcode/iOS SDK・実機・Androidイメージがなく、Launcher到達と今回の修正の実機動作は未確認です。**

| 領域 | 実装した内容 | 検証・制約 |
|---|---|---|
| iOS Runtime | 専用実行スレッドでQEMU shared libraryを起動、1プロセス1VM、全画面、ステータスバー／ホームインジケータ非表示、操作シート | Swift/ObjC++を含むXcodeビルドと実機UIは未検証 |
| CPU / JIT | universal protocolで準備したRX/RW aliasをTCGへ登録。iOSで別の未準備実行領域を確保しない | ホストで外部arenaの実使用とTCG実行を確認。TXM/SPTM実機は未検証。Goldfishは1 vCPU |
| Goldfish | PIC、bus、TTY、timer/RTC、NAND、可変寸法framebuffer、events、battery、audio、pipe、boot-properties、ADB | 実QEMUによるMMIO/DMAテスト成功。stock Android kernelとの互換性は未確認 |
| 描画 | guestのRGB565をBGRA32へ変換して受信。ページ切替時も同一行は転送しない。Metal専用queue、1フレームのみ進行、変更のない画面の再送抑制 | ホストで実ARMコードが書いた画素をcallbackで検証。iOS Metal描画は未検証 |
| 全画面 | 起動時の端末の画面比率に合わせて高さを決定。描画・guest ABS範囲・UIKit座標を共通寸法に設定 | 幅360/480/540/720を選択、高さ480〜1600の偶数。実行中のホスト回転ではアスペクト比を保持して表示 |
| タッチ／キー | 10点Protocol B、BQL下でinput queueをdrain、Back/Home/Recents/Power/音量、押下時間の保持、キュー満杯時の解除リトライ | native queueとguest capabilitiesを検証。実guestのジェスチャー／キー操作は未確認 |
| 外付けキーボード | タッチ専用化に伴い、guestには英字ハードウェアキーボードを宣言しない | Androidの画面キーボードを使用。完全な物理キーボード対応は対象外 |
| 音声 | GoldfishのPCMをAVAudioSourceNodeへ接続。render callbackは固定バッファ・allocation/lockなし。停止／再開ではリングを更新 | QEMU→hostの実PCM転送を検証。AVAudioSession、interruption、実音声は未検証 |
| Network | SMC91C111 + libslirp NAT、固定DHCP/DNS設定、公開port forwardingなし。NWPathMonitorでホスト接続を監視 | ホストでNIC/slirp初期化を確認。guest DHCP/DNS/インターネット疎通とiOSでは未検証 |
| ADB transport | Goldfish `qemud:adb` accept/start handshake、双方向の有界ring、backpressure、disconnect/reconnect | 合成ARMゲストとの双方向転送を実QEMU shared library上で検証。公開ADBソケットなし |
| ADB client / APK | classic CNXN/AUTH/OPEN/WRTE/OKAY/CLSE、Keychain RSA、sync SEND/DATA/DONE、pm install、shell/logcat、boot_completed判定 | AUTH/分割packet/shell/sync/キャンセル/RSA公開鍵形式のnative tests成功。実adbd認証・pm install・iOS Keychainは未検証 |
| Performance | guest更新数、GPU表示数、転送量、実メモリ使用量、TCG使用量／容量、TB flush、PCM欠損、thermal state | TCG容量を実エンジンで検証。iOSのFPS・input-to-photon・温度／消費電力は未測定 |
| イメージ | Filesから取り込み、sparse変換、上限／CRC／symlink検査、SHA記録、system read-only、userdata/cache書き込み | portable parser tests成功。iOS Files importer未検証 |
| ビルド／IPA | 固定UTMの必要依存だけをiOSへcross-build、QEMU frameworkと依存closureをIPAへembed、対応ソースを同梱Artifactとして生成 | スクリプト生成／構文／packagerの検査成功。macOS cross-build、GitHub Actions実行、実IPA成果物は未確認 |
| GPU acceleration | `qemu.gles=0`でguest software renderer + Metal framebuffer経路を選択 | ANGLE・emulator GLES command decoderは未実装。仕様で許容されたsoftware経路のguest起動も未確認 |

## この環境で実行した検証

- iOS用QEMUの公開シンボル一覧に埋め込み起動・JIT領域登録・metrics・ADBの9関数を追加。framework生成の前後に実Mach-Oの定義済み外部シンボルを検査し、欠落時はCIを失敗させる。アプリでも不足した関数名を表示する。packager回帰テスト6件成功。修正版IPAの実機起動は未確認。

- CIで報告されたMesonの`Executables ... are not runnable`に対応し、QEMUのcross fileに`needs_exe_wrapper = true`を追加。macOS用のnative compiler/SDKとiOS用compilerを明示的に分離。Meson 1.5上で同じエラーを再現し、指定追加後に設定が成功する回帰テストを確認。GitHub Actions全体の再実行結果は未確認。

- GCC 13でQEMU 10 + Goldfish shared libraryを`--enable-werror`でビルド。
- 実QEMUのGoldfish結合テスト9件成功。NAND読み書きと永続化、system保護、batch入力保存、MMIO再入拒否、timer/VSYNC/音声IRQ、input capabilities、pipe framingを確認。
- アプリと同じ埋め込みABIの結合テスト1件成功。外部RW/RX arena、ARM命令、540×1170 framebufferの画素、PCM、serial、input callback、ADB双方向転送、一時停止／再開／停止、再起動拒否、TCG容量を確認。
- Clang 18 / C++20 / ASan・UBSanでnative coreとADBの2テスト実行ファイルが成功。ADBでは分割read/write、AUTH署名／公開鍵の順序、shell、17,001-byte sync転送、キャンセル、RSA Montgomery形式を検証。
- Python toolingテスト15件成功（Meson 1.5を指定）。framework依存closure、macOS／他architecture／host依存の拒否、IPA内のarm64エンジンの必須検査、host launcher、project再生成を確認。
- shell/Python構文、固定UTMからの最小cross-build script生成を確認。

QEMUテストは合成したARM命令とデータだけを使用します。Androidの画面・boot_completed・Launcherを模擬して成功扱いにはしていません。

## 残る検証と開発

1. Xcode 26のMacまたはGitHub Actionsで、依存cross-buildからIPA生成まで実行し、iOS固有のビルドエラーがあれば解消する。
2. 実機へ全frameworkを含めて再署名・サイドロードし、modern JITの領域準備とQEMUの使用を検証する。
3. ユーザー提供のAPI 22 stock kernel/ramdisk/systemでbootログを取得し、必要なGoldfish/qemud互換性修正を進める。serial qemud fallbackや未対応pipeサービスは残っている。
4. Launcher、touch、音声、guestネットワーク、ADB許可画面、APKインストール、background／復帰を実測する。
5. 必要な性能に応じてANGLE/GLESを追加し、実機計測に基づいて調整する。現段階で高FPSやゲーム互換性を保証する計測結果はない。

## 起動ロゴの多重表示・色化けへの対応

AOSP android-goldfish-3.4の`drivers/video/goldfishfb.c`は`bits_per_pixel=16`、`line_length=width*2`、RGB565固定であり、FB_GET_FORMATを参照しません。従来のBGRA32固定読み取りは行幅・フレームサイズを2倍に誤認していました。guest形式をRGB565に合わせ、ホストへは不透明BGRA32へ変換します。画面末尾がRAM末尾に一致する場合、行端・上下端、RGB色、ページ切替、消灯／復帰を実QEMUのscreendumpで検証しました。

RGB565の前回表示内容を行単位で比較し、ページ切替や同一ページへの書き込みがあっても画素が変わっていない行は変換・転送しません。速度優先の幅360pxを追加し、新規設定の初期値にしました。既存の設定は保持されるため、ライブラリで360pxを選択してください。端末比率に合わせて全画面表示します。540px比の画素数削減は通常のiPhone比率で約56%であり、FPS向上率の実測値ではありません。

「APK・ADB → 起動診断を取得」でboot properties、uptime、meminfo、processes、直近のmain/system/crash logcatを収集します。自動起動確認で手動取得した診断出力が上書きされないよう変更しました。起動ロゴから進まない原因は実機ログ待ちであり、描画修正だけでLauncher到達が解決したとは判断していません。

参照: https://android.googlesource.com/kernel/goldfish/+/refs/heads/android-goldfish-3.4/drivers/video/goldfishfb.c

## 24分間起動ロゴから進まないログへの対応

提供されたシリアルログでは、kernel起動、SELinux policy読込、/systemと/dataのext4マウントは成功しています。一方でcache用の第3MTDが存在せず、`/dev/block/mtdblock2`のopen/mountと`fs_mgr_mount_all`が失敗しています。これは確認できた構成不備であり、ログだけでsystem_serverの状態やロゴ停滞の全原因までは判定できません。

iOS起動前にcache.imgがない場合、アプリ内の空の64MiB ext4テンプレートを既存のnative sparse importerで展開してcache.imgとして確定します。既存のcache/system/userdataを上書きせず、処理はImageStore actor上で行います。iOS VMはcacheを含む3パーティションを必須としました。空のファイルシステムメタデータだけを生成するもので、Android OSイメージやユーザーデータは同梱しません。

テンプレートはビルド時にe2fsprogsで生成します。ext4機能をhas_journal/ext_attr/filetype/extent/sparse_super/large_fileに限定し、root所有者は0:0、inode table/journalは初期化済みとします。生成したsparseをアプリと同じC++ importerで展開し、e2fsck -fn成功、機能ビット、既存ファイルの上書き拒否を検証しました。実QEMUではcacheが第3パーティションとして独立して読み書き・永続化されることを確認（Goldfish 10テスト成功）。IPA検査にもテンプレート必須条件を追加しました。

ツールテストは19件中18件成功・1件はローカルMeson 1.3によるスキップです。修正IPAの実機起動完了は未確認です。症状が残る場合は、APK・ADBの起動診断（logcatとprocess一覧）でAndroid userspace側を調べる必要があります。RAM 1GiB指定がkernelで760MiBへ切り詰められている点もログで確認できましたが、今回の変更ではRAM設定を自動変更しません。

## 起動確認後の操作・性能改善

ユーザーからAndroidのホーム・設定・ブラウザが動作したと報告されています。今回の追加変更についてはiOS実機未検証です。

- API22標準qwerty2.idcを選択するデバイス名へ変更。Goldfish 3.4はINPUT_PROP_DIRECTを取り込まないため、独自名でのポインタ判定を回避します。相対マウス軸と物理英字キーボードを宣言せず、10点の絶対マルチタッチとAndroidの画面キーボードを使います。ホームボタンはqwerty配置の102へ変換し、履歴はAndroidのナビゲーションバーを使います。
- Metal drawableの画素数をguest画素数の範囲へ制限し、Retina解像度での不要な拡大描画を削減。静止画面ではdirty bitmapのスナップショット作成を省略し、同一座標のtouch moveもキューへ送信しません。RAMはstock kernelが使える760MiBを上限として渡します。
- 描画と入力のビューを共通のsafe areaに配置。起動時はアクティブwindowの寸法を参照し、横長ウィンドウでもguest panelを縦長に維持します。回転・resize後はaspect fitで余白表示し、切り抜きや非等方拡大をしません。実行中のguest解像度変更やAndroid自身の自動回転は未実装です。
- メイン画面はイメージと起動操作を中心にし、メモリ/JIT/診断を詳細設定へ移動。StikDebug準備後、アプリがforegroundに戻ってから自動起動します。StikDebugによる準備自体は引き続き必要です。
- 完全全画面を初期値にし、メニューと性能表示を非表示。3本指0.7秒長押し、またはアクセシビリティアクションで操作メニューを開きます。全画面の選択はUserDefaultsへ保存します。
- 既存userdata/cacheと設定の永続化を維持し、pause/stop時にQEMU block flushを追加。pause時はiOS background taskで書き出し時間を確保します。イメージ置換前にデータも置き換わることを確認します。プロセス強制終了時のguest未書込RAMや実行状態の復元は保証しません。

QEMUをWerrorで再ビルドし、Goldfish10件・埋め込みライフサイクル1件、ASan/UBSan native2件に成功。native側ではSE/ノッチ端末/iPad/狭いウィンドウに相当するviewportで座標変換を確認しましたが、UIKit/Metalの実機テストの代わりではありません。FPSや入力遅延の改善率は未測定です。

参照: https://android.googlesource.com/platform/frameworks/base/+/android-5.1.1_r38/data/keyboards/qwerty2.idc

## StikDebug直接起動・旧iOS・Androidバージョン受付

起動/foreground復帰時に250ms間隔のプロセス状態監視を開始し、CS_DEBUGGEDを確認すると手動Wait操作なしでJIT領域を準備します。universal経路ではP_TRACEDも確認します。領域準備失敗後は自動で繰り返さず、Ready後は監視を終了します。旧経路ではTXM/SPTMのSPI取得失敗を開始の妨げにせず、iOS26以降の保護方式は取得失敗時に停止します。Enable with StikDebugボタンは削除し、Android起動ボタンの連携と詳細設定の手動Waitは維持しました。

最低OSを17.0へ変更し、core/QEMU/依存sysroot/framework plistも統一。旧26.0向けframeworkが混ざるとパッケージ検査で失敗させます。iOS26専用scene geometry APIには可用性分岐を追加しました。iOS16以前は今回のビルド対象外です。旧/新方式とも、この変更を含む実機試験は未実施です。

AndroidはAPI14〜23（Wear20を除く）のARMv7 Goldfish/ext4を受付対象とし、API別の保存profileを導入。旧API22 profileも読み込めます。APIとPlatform.Versionの矛盾、異なるCPU/board/tagを拒否し、system/userdata/cacheのextスーパーブロックを検査します。古い4系のYAFFS2・F2FSを含む全面的な対応は未実装です。受付条件の拡大はAndroid4/6の起動実証ではありません。SwiftモデルのAPI範囲・旧profile・JIT URLテストを追加しましたが、Swift/Xcodeの実行環境がないためローカルでは実行できていません。
