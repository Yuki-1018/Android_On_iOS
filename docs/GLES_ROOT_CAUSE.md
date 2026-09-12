# Android 6 Browser / WebViewのGPUクラッシュ調査

現状: 原因の経路は特定済み。GLES 2対応レンダラーは未実装で、Browser / WebViewの正常起動・描画はまだ確認できていない。

## 現在の実装とログの対応

- `QEMUBridge/VMController.mm`のカーネル引数、および`ThirdParty/AndroidQemuCompat/qemu/pipe.c`の起動プロパティは`qemu.gles=0`を指定している。
- Android 6の[EGL Loader.cpp](https://android.googlesource.com/platform/frameworks/native/+/android-6.0.1_r1/opengl/libs/EGL/Loader.cpp)は`ro.kernel.qemu=1`かつ`ro.kernel.qemu.gles=0`なら`/system/lib/egl/libGLES_android.so`を固定で選択する。
- この[libaglのEGL設定](https://android.googlesource.com/platform/frameworks/native/+/android-6.0.1_r1/opengl/libagl/egl.cpp)は`EGL_RENDERABLE_TYPE = EGL_OPENGL_ES_BIT`であり、GLES 2用の`EGL_OPENGL_ES2_BIT`を提供していない。Loaderは取得できないGL関数を`gl_unimplemented`へ置き換える。このため「called unimplemented OpenGL ES API」と整合する。
- 提供されたログではChromiumのEGL config探索とGLSurface初期化が失敗し、続いてGpuThreadがCHECK失敗でSIGABRTしている。これはiOS上のMetalビューの表示サイズや色変換ではなく、ゲストに必要なGL実装がないことが主因と判断できる。
- `Display/MetalDisplay.mm`（表示処理）とGoldfish `display.c`はCPUフレームバッファを表示する経路であり、ゲストのGLES命令やEGLContextを実行していない。Metalで画面を表示できることはゲストのGLES 2対応を意味しない。

## 既存Android Emulatorとの比較

[AOSP Android 6 HostConnection.cpp](https://android.googlesource.com/device/generic/goldfish/+/android-6.0.1_r1/opengl/system/OpenglSystemCommon/HostConnection.cpp)は、通常QemuPipeStreamでホストに接続し、GLES 1・GLES 2・renderControlのencoderを利用する。現在の`pipe.c`はboot-properties、ADB、gsm、pingpongを実装しているが、このGPU経路を実装していない。

参照用に取得済みのAOSP QEMU `android/opengles.c`は、`libOpenglRender`をロードし、renderer初期化・post callback・GPU pipeの登録を行う。元の[EmuGL FrameBuffer.cpp](https://android.googlesource.com/platform/external/qemu/+/e6aef36e024c3265ff8103f8d2265dd235851ef4/distrib/android-emugl/host/libs/libOpenglRender/FrameBuffer.cpp)は実際のEGLDisplay・EGLContextとGLES dispatchを必要とする。これらは現在の最小iOSエンジンの依存関係に含まれていない。

`qemu.gles=1`だけを設定すると、存在しないホストrendererへの接続を選択することになり、正常な描画経路にならない。Browserの起動引数やHWUIの一括無効化でも、不足したGLES 2実装を補うことはできない。今回そのようなフラグ変更・成功応答の偽装は実施していない。

## 根本修正に必要な実装

1. Android 4〜6のGLES 1 / GLES 2 / renderControlプロトコルと互換なホストdecoderを組み込む。分割パケット、同期応答、複数ゲストプロセス・スレッドの接続を扱う。
2. iOSで実際に動くEGL/GLES backendに接続し、context・surface・texture・color buffer・EGLImage・共有context・同期の寿命を管理する。既存desktop EmuGLのライブラリを単純にiOSへコピーするだけでは成立しない。
3. grallocによるbuffer登録・更新・読み戻しと画面postを接続し、CPUフレームバッファ経路との整合を取る。
4. backendの実初期化とGLES 2 configの確認が成功したときのみGPU対応をゲストへ通知する。
5. API23の実イメージ上でEGL初期化、GLES2のshader compile/link、描画・readback、texture/FBO/EGLImage、複数context・複数アプリ、Browserと独立WebViewアプリを検証する。

本環境にはiOS SDKおよび検証用Androidシステムイメージがなく、このrendererの実装・統合・実機検証は完了していない。UI変更・ダウンロード対策の完了と、GPU修正の完了は別である。
