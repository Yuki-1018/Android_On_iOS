# AndroidEmu

`CODEX_ANDROID_EMULATOR_COMPLETE.md`を目標とする、iOS 17+実機arm64向けAndroid 5.1.1 / API 22 / ARMv7 Goldfishエミュレータです。

iOS用の全画面Runtime、QEMU shared library起動、modern JIT領域の受け渡し、Metal描画、タッチ、音声、NAT、ADB/APK操作を実装しています。**Linuxで埋め込みエンジンを検証しましたが、iOSビルド・実機でのAndroid起動・Launcher到達は未確認です。** 詳しい到達点は[実装状況](IMPLEMENTATION_STATUS.md)を参照してください。

Android OS/kernelイメージ、APK、Google Play Services/GMSは同梱・取得・外部送信しません。利用可能なAndroid **5.1.1 / API 22 / default / armeabi-v7a / Goldfish**イメージを自分で用意します。App Store向けではなくサイドロードを前提とします。

## アプリの操作

1. Filesからイメージフォルダを「Add Android」で取り込みます。
2. Guest RAMとTCG cacheを設定し、StikDebugまたは対応universal debuggerでJITを有効にします。
3. JIT Readyになったら「Androidを起動」を選びます。QEMU frameworkが欠けたビルドでは起動できません。
4. Androidの実フレームバッファを全画面に表示します。右上の「…」から一時停止、Androidキー、性能表示、起動ログ、APK・ADBを開きます。

画面は起動時の端末比率に合わせます。タッチは直接10点Protocol Bとして渡し、Androidキーは押している時間を保持します。外付けキーボードは物理HIDキーをguestへ送ります。システム画面を模した静止画や疑似Android UIは表示しません。

backgroundまたはメモリ警告ではVMを一時停止します。復帰は操作シートから行います。QEMUの安全な再初期化が未検証のため、**停止後の再起動にはアプリを終了して開き直す必要があります。** userdata/cacheは永続化されますが、停止時にguest内の未保存データが失われる場合があります。

## イメージフォルダ

```text
Android51/
  kernel-qemu             # または kernel。ARM zImage
  ramdisk.img
  system.img
  userdata.img
  source.properties       # 必須
  cache.img               # 任意。未提供ならiOSアプリが空の64MiB ext4を作成
  hardware-properties.ini # 任意
```

ZIPを展開してからフォルダを選択します。`source.properties`で`AndroidVersion.ApiLevel=22`、`SystemImage.Abi=armeabi-v7a`、`SystemImage.TagId=default`を検査します。元ファイルは変更せず、Application Support/Android51へコピー・sparse変換します。kernel/ramdisk各64 MiB、ディスク各8 GiB、合計16 GiBまでです。実行時のrawディスクは4096-byte alignmentが必要です。

メタデータとkernelヘッダの確認は、system内部のバージョンやGMS不在を保証しません。SHA-256は取り込み記録であり公式署名の検証ではありません。

## JIT

iOS 17+実機と、再署名時の`get-task-allow=true`が必要です。StikDebugは別アプリです。TXM/SPTMに応じたuniversal protocolでRW/RX aliasを準備し、生成コードの実行検査後に同じ領域をTCGへ渡します。古いJIT方式やTCIへのフォールバックはありません。

URL起動成功だけではReadyにしません。通常のLLDBはuniversal scriptの代わりにならず、未処理のbreakpointで終了する場合があります。領域準備後のcache変更や失敗後の再準備にはアプリの再起動が必要です。実機でのTXM/SPTM検証はまだ行えていません。

## ビルドと未署名IPA

macOSとXcodeのiPhoneOS SDK 26以降が必要です。

```sh
brew install meson ninja pkg-config gettext glib autoconf automake libtool e2fsprogs
export PATH="$(brew --prefix gettext)/bin:$PATH"
bash scripts/bootstrap.sh
bash scripts/test_models.sh
bash scripts/build_dependencies.sh
bash scripts/build_ios.sh
bash scripts/package_unsigned_ipa.sh
python3 scripts/collect_engine_source.py
```

`build_dependencies.sh`はportable coreに加え、固定UTMのbuild machineryから必要なlibffi/iconv/gettext/glib/pixman/slirp/libucontextだけを構築し、Goldfish用QEMUをiOS shared libraryへビルドします。`build_ios.sh`はエンジンと依存frameworkのclosureをアプリへ配置します。macOSバイナリや未同梱のhostライブラリ依存はpackagerが拒否します。

GitHub Actionsにも同じ経路を追加しています。成功時の`AndroidEmu-development-unsigned` ArtifactにはIPA、entitlements、再署名メモ、アプリの対応ソース、実際のQEMU・依存ソースが含まれます。エンジンを欠いたIPAは検査で失敗します。**この環境ではActions／Xcode実行と実IPA生成は未確認です。**

再署名では`Frameworks/`の全frameworkを先に署名し、最後にアプリへJIT用entitlementsを適用します。未署名IPA自体には有効なentitlementsはありません。

Increased Memory Limitは任意です。対応するApp ID・プロビジョニングプロファイルで署名する場合だけ、同梱の`AndroidEmu-increased-memory.entitlements`をメインアプリに使用してください。通常は従来の`AndroidEmu.entitlements`を使用します。実際の署名資格と`os_proc_available_memory()`の余裕を確認し、TCGキャッシュを自動で384／512 MiBに増やします。AndroidのRAMはGoldfishカーネルの制約で最大760 MiBのままです。資格だけで全端末の利用可能メモリが増える保証はありません。[Appleの仕様](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.increased-memory-limit)

通信はSDK標準RIL向けの仮想データモデムからNATへ接続します。Android上では携帯データ接続として扱われますが、iPhoneのSIMを操作しません。ホストでのUDP往復は検証済みで、実機のDNS・Web閲覧は未検証です。Android 6取り込み時のメモリ圧迫対策として、SHA-256処理の一時データをチャンクごとに解放し、sparse空領域のCRC検証を高速化しています。実際のクラッシュログとの照合・解消確認はまだ必要です。

## ホストでの検証

Linuxの依存:

```sh
sudo apt-get install build-essential ninja-build pkg-config python3-venv libglib2.0-dev libpixman-1-dev libslirp-dev libfdt-dev zlib1g-dev
python3 scripts/fetch_references.py --only qemu
bash scripts/build_qemu_host.sh
python3 Tests/QEMU/test_goldfish.py
python3 Tests/QEMU/test_embedded.py
cmake -S . -B build/native -DEMU_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native --parallel 2
ctest --test-dir build/native --output-on-failure
python3 -m unittest discover -s Tests -p 'test_*.py' -v
```

埋め込みテストはアプリと同じABIを呼び、実TCG・外部arena・画素・PCM・ADB pipe・pause/resume/stopを検証します。Android OSのテストではありません。

自分のrawイメージでheadless起動を試す場合:

```sh
python3 scripts/run_android_host.py /path/to/Android51 --qmp /tmp/android51-qmp.sock
```

systemはread-only、userdata/cacheは書き込み可能です。作業用コピーを使ってください。QMPは指定したローカルUnixソケットだけを開きます。

## 実装上の制約

- 元のGoldfish boardにSMP起動経路がないため1 vCPUです。2 vCPU/MTTCGを動作確認した機能として表示しません。
- GLES 1/2・renderControlのホスト経路を実装し、iOSではANGLE Metalへ接続します。CPUフレームバッファは起動初期の表示に使います。実iOS端末とAndroid Browser/WebViewでの検証はまだ必要です。
- 実guestのkernel/ramdisk互換性、Launcher、音声、DHCP/DNS、APKインストールはまだ確認できていません。
- ADBはアプリ内transportです。初回はAndroid側のUSBデバッグ許可が必要な場合があります。APKは512 MiBまで、shell出力とserialログは有界です。
- 性能表示は実カウンタです。未測定のinput-to-photonや架空のFPSは表示しません。

ライセンスと出典は[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)、[ThirdParty/README.md](ThirdParty/README.md)、[Goldfish integration](ThirdParty/AndroidQemuCompat/README.md)を参照してください。

### JITとゲストバージョンの追加対応

StikDebugから直接起動・復帰した場合も、デバッガによるJIT許可を自動監視し、領域準備と自己テストを行います。「Enable with StikDebug」は削除しました。通常のAndroid起動ボタンからのStikDebug連携は維持しています。iOS17/18ではlegacy経路、iOS26以降ではTXM/SPTM検出に応じたuniversal経路を使用します。最低OSをアプリ・core・QEMU・依存frameworkすべて17.0へ統一し、17.0より新しいOSを要求するframeworkはパッケージ時に拒否します。ビルドには引き続きXcode26 SDKが必要です。この変更の実機検証は未完了です。

イメージ受付はAPI14/15/16/17/18/19/21/22/23のARMv7 Goldfishへ拡大しました。defaultイメージが対象で、古いAPI18以前はタグ省略も許可します。Google APIs、Wear、x86、arm64、Ranchu、YAFFS2/F2FSは対象外です。ext4形式を読み取りで検査し、不適合なイメージを上書き変換しません。ユーザーから起動確認が得られているのはAndroid5.1.1であり、4系・6系の互換性は検証中です。

## プロファイルとイメージのダウンロード

ライブラリでプロファイルを追加・切替・名前変更・削除できます。イメージとuserdata/cacheは`Application Support/Profiles/<UUID>/`で分離します。従来の`Android51/`は移動せず「これまでのAndroid」として登録し、保存済みデータを引き継ぎます。削除には確認が必要です。QEMUはプロセスあたり1回起動のため、Androidを実行した後に別プロファイルを起動するにはiOSアプリを終了して開き直してください。

「Androidイメージをダウンロード」は`https://api.yuki-0604.f5.si/images.json`の`{"images":[{"name":"Android 6.0","url":"https://example.com/android6.zip"}]}`形式を読みます。HTTPSのみ対応し、一覧取得失敗時は再試行または従来のフォルダ取り込みを選べます。実サーバーからのダウンロードは未検証です。

ZIPには、通常のフォルダ取り込みと同じ`source.properties`・ARMv7 Goldfishの`kernel-qemu`（または`kernel`）・`ramdisk.img`・`system.img`・`userdata.img`を1組含めてください。サブフォルダ内でも構いません。`cache.img`は任意です。stored/deflate形式、ZIP全体4 GiB未満、展開後合計16 GiB以下に対応します。ZIP64中央ディレクトリ・暗号化ZIP・リンクは非対応です。ZIPのCRCと既存のイメージ検証に成功してから、新しいプロファイルを公開します。既存プロファイルをダウンロードで上書きしません。展開と取り込みで追加の空き容量が必要です。

## ADBとAndroid 6のアプリ停止

ADBの認証／通常応答待ちは120秒、APKインストールの応答待ちは600秒です。バックグラウンドの起動確認はadbd不在時にキューを占有しません。APK転送はclassic ADBのパケット内にsyncヘッダーを含め、余分な往復を削減しています。連続する転送で遅れて届くclose応答を処理し、具体的なエラーを表示します。一時停止中はADB画面からAndroidを再開してください。初回のUSBデバッグ許可はAndroid側で承認が必要です。

Android 6のBrowser / WebViewが必要とするGLES 2用EGL configを提供するため、Goldfishの`opengles`パイプ、AOSP EmuGLデコーダー、iOS向けANGLE Metal、gralloc表示転送を接続しました。レンダラーの実初期化後にGPU対応を通知します。Browser固有の起動オプションによる回避は使用しません。Linux上では実GLES2のシェーダー描画・読み戻し・gralloc転送・複数コンテキストとQEMU MMIOを検証済みですが、iOSビルドと実Androidアプリの起動確認は未完了です。[実装と検証範囲](docs/GLES_ROOT_CAUSE.md)を参照してください。

## ライブラリUIと通信の回復

UTMの一覧・詳細の構成を参考に、Androidごとの一覧行と詳細画面へ整理しました。iPadでは左右に表示し、iPhoneでは一覧から詳細へ移動します。＋からダウンロード／フォルダ取り込みを選ぶと新しいAndroidを追加します。管理メニューで名前変更・置き換え・削除を行い、詳細設定・JIT情報は歯車から開きます。空のライブラリと最後のAndroidの削除にも対応しています。

ダウンロードは一時的なネットワーク障害とHTTP 408/429/500/502/503/504に最大3回再試行します。Retry-Afterがある場合はその待ち時間を尊重し、60秒を超える場合は自動再試行せずエラーを表示します。URLSessionが再開データを返した場合は途中再開を試みます（サーバーのRange・ETag/Last-Modified対応が必要で、最初からの取得になる場合もあります）。再開情報は実行中のメモリ内だけに保持し、アプリ終了後の再開には未対応です。

接続待ち・再試行・進捗・空き容量不足を表示します。キャンセル、証明書エラー、404、展開・検証失敗は自動で繰り返しません。Swiftの再試行・途中再開情報の受け渡し・上限・空ライブラリのテストを追加しましたが、このLinux環境ではSwiftUI/iOSビルドとSwiftテストを実行できていません。

### RAM 3 GB端末向けの描画・メモリ制御

RAM 3 GB以下ではTCGキャッシュを最大128 MiB、ゲストRAMを最大640 MiBへ制限します。任意のIncreased Memory Limit資格でもこの物理RAM制限を優先します。既定の画面幅360 pxを維持し、GPUの同一画面転送を省略、変更行のみコピー・Metalアップロードします。GPU通信はfd通知で起こし、一定間隔のGPUポーリングを行いません。クライアントごとの待ちを分離し、大容量転送後の一時バッファを縮小します。実iPad 9でのFPS・ピーク使用量は未測定です。

ホスト側の実描画テストは `cmake -S ThirdParty/EmuGL -B build/emugl-host -DEMUGL_TESTS=ON`、ビルド後 `ctest --test-dir build/emugl-host --output-on-failure` で実行できます。LinuxではEGL/GLESとMesaのソフトウェアレンダラーが必要です。GPU付きホストQEMUは `ANDROID51_GPU=1 QEMU_BUILD_DIR=build/qemu-gpu bash scripts/build_qemu_host.sh` でビルドします。
