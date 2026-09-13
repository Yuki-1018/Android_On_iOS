# ARMv7のメモリ指定

設定値は512〜4096 MiB、1 MiB単位。JSONは従来どおり整数なので、旧プロファイルの512・640・768・1024も読み込める。

- 512〜760 MiB: インポートしたイメージのカーネルを使用。
- 761〜4080 MiB: 同梱の`goldfish-highmem.zImage`を使用。
- 4081〜4096 MiB: 同梱カーネルを使用し、機器用アドレスを除いた4080 MiBを渡す。設定画面には指定値と使用量の両方を表示。
- メモリ不足時は起動前にエラーを表示し、指定値を黙って減らさない。描画/UI用の256 MiBも必要量に含める。この事前確認は、起動後のすべてのメモリ圧迫を保証するものではない。

既定640 MiBは変更しない。RAM 3 GB以下の端末でも手動設定できるが、TCGキャッシュの低メモリ向け上限は維持する。Increased Memory Limit資格はホスト側の予算に作用し、ゲストのアドレス空間を広げるものではない。

## カーネルとボード

以前の提供ログには`Truncating RAM ... (vmalloc region overlap)`と`0K highmem`がある。これは[Goldfishのmmu.c](https://android.googlesource.com/kernel/goldfish/+/880d9af358076df842377facd4b900f6bbecc783/arch/arm/mm/mmu.c)の`CONFIG_HIGHMEM`無効時の経路であり、QEMUの`-m`だけを増やしても解消しない。

同梱カーネルはAOSP Goldfish Linux 3.4.67を公式`goldfish_armv7_defconfig`からビルドする。この設定はHIGHMEM、ARMv7、VFP/NEON、Binder、ashmem、ext4、SELinuxを有効化する。ソースには変更を加えない。

現ボードのRAMは物理アドレス0から連続する。機器は`0xff000000`以降に存在し、Cortex-A8は32bit物理アドレスなので、4096 MiB全体を連続RAMにはできない。QEMU側も4080 MiB超を拒否し、機器とRAMを重ねない。ARM64対応やLPAE対応を意味しない。

## ビルド・検証

Linuxで`python3 scripts/build_highmem_kernel.py`を実行する。固定リビジョンとSHA-256を照合したソース／GCC 4.8を使用。出力は`build/guest-kernel/`。macOSではCIの`goldfish-highmem-kernel`成果物をこのディレクトリに置いてiOSアプリをビルドする。

`python3 Tests/QEMU/test_highmem_kernel.py`は、実QEMUで2048／4080 MiBのLinuxを起動し、ARMユーザー空間から1 GiBの全ページに書き込み・読み戻しする。両容量で成功を確認した。4096 MiBをQEMUへ直接渡した場合の拒否も検査する。

これはAndroid 4/5/6のフレームワーク起動やiOS上のJetsam耐性の検証ではない。iOS SDK・実機のない環境では、それらは未検証。761 MiB以上で互換性問題が出た場合、760 MiB以下に戻せば付属カーネルに戻る。プロファイルのイメージやデータを書き換えない。

CIはカーネルとテスト用コンパイラをキャッシュし、起動テストは毎回実行する。カーネルのCOPYINGをアプリへ、対応する全ソース・実際のconfig・ビルド情報をIPAと同じ成果物へ収録する。
