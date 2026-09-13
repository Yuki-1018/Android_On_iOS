# Android Browser / WebViewのGPU経路

実装状況: ホストレンダラー、Goldfish pipe、gralloc表示、iOS ANGLEビルド・依存ライブラリ梱包を追加した。Linuxで実GLES2描画とGPU付きQEMUのテストは成功。iOS SDKがない本環境では、iOSコンパイル・ANGLE Metal実機動作・Android Browser/WebView起動は未検証であり、「全端末で完全解決」とはまだ確認できない。

## 原因

旧実装は`qemu.gles=0`を指定し、GPU pipeもなかった。[Android 6 EGL Loader](https://android.googlesource.com/platform/frameworks/native/+/android-6.0.1_r1/opengl/libs/EGL/Loader.cpp)はこの場合`libGLES_android.so`を選ぶ。[そのEGL設定](https://android.googlesource.com/platform/frameworks/native/+/android-6.0.1_r1/opengl/libagl/egl.cpp)はGLES1の`EGL_OPENGL_ES_BIT`だけを提供するため、GLES2を必要とするChromiumのconfig探索・GLSurface初期化が失敗する。CPUフレームバッファをMetalで表示するだけではゲストのGLES2実装にならない。

## 追加した描画経路

`guest EGL / GLES → pipe:opengles → socketpair → EmuGL GLES1/GLES2/renderControl decoder → ANGLE EGL/GLES → Metal`

`gralloc color buffer → EGLImage / pbuffer blit → readback → BGRA変更行 → QEMU display → MetalDisplay`

- AOSP EmuGLの既存context・surface・color buffer・EGLImage管理を利用。出典と変更点は[UPSTREAM.md](../ThirdParty/EmuGL/UPSTREAM.md)に記載。
- 先頭clientFlagsと分割された命令、部分読み書き、バックプレッシャー、複数接続に対応。QEMUのfd通知を使用し、GPUの1 msポーリングは撤去。
- FrameBufferのblitterをデスクトップ窓なしで初期化。GLESシェーダーのvarying精度を一致させ、gralloc色バッファの上下反転を一度だけ適用。
- 共有資源はFrameBufferのロックで保護。応答書き込み中に全接続のロックを保持しないため、停止したアプリが他の描画接続を止めない。
- 起動時に実EGL/GLES2 contextとEGLImage対応を確認してからカーネル引数・boot-propertiesに`qemu.gles=1`を設定。iOSビルドではバックエンド初期化失敗をエラーとして扱う。
- ANGLEの実装はGLES1をフロントエンドで提供する。Linuxテスト用MesaでGLES1 contextを生成できなかった場合は、使えないGLES1 configを非公開にしてGLES2を検証する。Linuxテスト成功はiOS上のGLES1成功の証明ではない。
- ゲストに伝えるGLバージョンとGLES2拡張を、旧Goldfishプロトコルが扱える範囲に制限。GLES3・Vulkan・ARM64対応を意味しない。

## 低メモリ端末向け

RAM 3 GB以下の端末は、TCGキャッシュ最大128 MiB・ゲストRAM最大640 MiB。画面幅は既定360 px。GPU投稿の同じ行は再変換せず、同一画面なら転送せず、変更行だけをQEMU・Metalへ渡す。大きなアップロード後は受信バッファを64 KiBへ縮小し、大きな応答バッファも保持し続けない。フレームは最新状態へ集約し、無制限の描画キューを作らない。

## 検証

`Tests/GPUTests.cpp`は実EGLバックエンドと実デコーダーを使い、以下を検証する。

- 分割ハンドシェイク・命令・応答、読み取りが停止した接続からの分離
- GLES2 config/context/pbuffer、頂点・フラグメントshaderのcompile/link、VBOでの実描画、ピクセルreadback
- gralloc色バッファのflush/post、BGRA色順・上下方向
- 同じ画面では転送0回、1ピクセル変更では変更した1行だけの転送
- 複数接続・共有contextでのprogram共有と資源解放

ASan/UBSanでも描画テストを実行。Mesaのプロセス寿命キャッシュはリーク検査から除外。さらにGPU付きQEMUで14件のMMIO・TCG・pipe回帰テストを実行した。

残る実機確認は、iOS 17/18/26の署名ビルド、Android 4〜6各SDKイメージ、Browser・独立WebViewアプリ・GLES1/2アプリ、バックグラウンド復帰、iPad 9でのFPSとピークメモリ。これらの結果を未測定の速度や互換性の保証に置き換えない。

## 起動直後のNULL実行とANGLEローダー

固定版WebKitの[EGLビルド設定](https://github.com/utmapp/WebKit/blob/ed78ab6e1a37f4f11583a0bd038f22ec91f3ff10/Source/ThirdParty/ANGLE/Configurations/EGL-dynamic.xcconfig)は`ANGLE_USE_EGL_LOADER`と`ANGLE_DISPATCH_LIBRARY="GLESv2"`を指定する。[libEGLの実装](https://github.com/utmapp/WebKit/blob/ed78ab6e1a37f4f11583a0bd038f22ec91f3ff10/Source/ThirdParty/ANGLE/src/libEGL/libEGL_autogen.cpp)はロード失敗後も`EGL_GetProcAddress`の内部ポインタを呼ぶ。Framework化で実行ファイル名・配置を変える本アプリでは、この検索経路を使用しない。

EmuGLは実装側`libGLESv2`の公開関数`EGL_GetProcAddress`へ直接リンクする。通常のMach-O依存として梱包され、`@rpath/libGLESv2.framework/libGLESv2`へ書き換わる。ビルド時のシンボル検査と、起動時のホスト描画用関数検査を追加した。関数が不足する場合は、その名前を起動エラーへ返す。

`ANGLEBackendTests.cpp`は実際のMetal用Backend.cppをホスト上でコンパイルし、旧libEGL入口を呼ぶと異常終了する疑似ライブラリで、実装側への直接接続とNULL検出を検証する。実iOSの動作確認を代替するものではない。

## EGLImage作成の不足

固定版Metalの`DisplayMtl`は`EGL_KHR_image_base`を公開するが、`EGL_KHR_gl_texture_2D_image`は公開していない。`ImageMtl::initialize`も`EGL_METAL_TEXTURE_ANGLE`のみ実装していた。image_baseだけではGLテクスチャ由来の画像作成を保証しないため、EmuGLの検査を無条件に通す修正はしない。

`angle_metal_image.py`でGLテクスチャの指定mipのMetalストレージを共有し、元テクスチャの削除・再定義時もEGLImage側で旧ストレージを保持する処理を追加した。対応拡張はこの実装と一緒に有効化する。ビルドキャッシュキーにパッチ本体も含める。

`EGLImageTests.cpp`は画像作成、共有元の部分更新、サイズ変更による再定義、元テクスチャとEGLハンドル削除後の読み戻しを検査する。同じサイズでの再定義はホストMesaで旧ストレージが上書きされるため、別の`--same-size`ケースとしてApple/ANGLEビルドで実行する（本環境では未実行）。Linux/Mesaでの成功だけでは、このMetalパッチの実機検証完了を意味しない。
