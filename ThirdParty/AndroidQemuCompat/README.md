# Android51 Goldfish port

This directory contains an ARMv7 Goldfish machine overlay for pinned UTM QEMU 10, derived from [AOSP external/qemu at Android 5.1.1 r38](https://android.googlesource.com/platform/external/qemu/+/006a174244e336c578f45bf78a9dbdbd890c217d/). It is a shared engine with a C ABI used by the iOS runtime and host integration tests. Android Launcher and iOS execution have not been validated.

`scripts/prepare_qemu.py` verifies the exact UTM revision, installs the isolated board sources, selects its Kconfig dependencies, and applies patches idempotently. `scripts/build_qemu_host.sh` builds the real `qemu-system-arm`; see the root README for commands. Other QEMU boards are excluded.

## Device contract

| Address / IRQ | Implementation |
|---|---|
| RAM at 0 | 128–1024 MiB, Cortex-A8, one CPU, ARM boot loader, machine ID 1441 |
| `ff000000` | Legacy PIC; pending register returns the lowest IRQ number |
| `ff001000` / 1 | Platform enumeration, guest-virtual name transfer |
| `ff002000` / 4 | Goldfish TTY, bounded receive queue, character and buffer I/O |
| `ff003000` / 3 | Virtual nanosecond timer, absolute alarm |
| `ff004000` / 16 | Double-buffered 44.1 kHz stereo S16LE output through QEMU audio; no capture |
| `ff010000` / 11 | Goldfish wall-clock RTC |
| `ff020000` / 10 | QEMU SMC91C111; host launcher selects slirp user networking |
| `ff030000` | NAND v1, raw system/userdata/optional cache backends; read-only system, batch operations |
| `ff040000` / 12 | RGB565 guest framebuffer (default 540×960) converted to BGRA32, unchanged-row suppression across page flips, virtual 60 Hz VSYNC and base-update IRQ |
| `ff050000` / 13 | Linux input capabilities, ten MT Protocol B slots, bounded event queue |
| `ff060000` / 14 | Minimal AC/full battery reporting |
| `ff070000` / 15 | Goldfish v1 pipes: connector, bounded pingpong, framed qemud boot-properties, SDK data modem and in-process ADB |

The original Goldfish board has no SMP startup path. This implementation therefore uses one vCPU; enabling a second CPU or MTTCG is not a tested performance option. The iOS app chooses a bounded panel height to match the launch window; input axis maxima use the same board dimensions.

Guest-virtual DMA translations must point into board RAM; physical batch descriptors are also bounded to RAM. MMIO recursion and oversized transfers are rejected. NAND raw ext4 geometry is 512-byte pages, zero spare bytes, 4096-byte erase units. No image is formatted or fetched. Persistent userdata/cache writes occur when running the host launcher.

The input device uses the stock API22 `qwerty2.idc` touchscreen classification. The old Goldfish kernel does not import INPUT_PROP_DIRECT, so a custom device name could be treated as a pointer. Relative axes and alphabetic hardware keys are not advertised, allowing Android's on-screen keyboard. The host Home control maps to scan code 102 for qwerty. Recents uses Android's on-screen navigation bar. Full physical-keyboard emulation is no longer advertised by this touch-only device.

## TCG patches

`android51_tcg_set_region(rw, rx, bytes)` accepts disjoint, page-aligned prepared aliases once, before QEMU initialization on the launch thread. The allocator consumes that region and rejects an unprepared allocation on physical iOS. APRR switching and the older UTM breakpoint path are disabled for that iOS path. macOS/Linux retain host allocation. The app supplies `AEJITWritableBase`, `AEJITExecutableBase`, and `AEJITArenaSize` (excluding the JIT self-test page). The iOS `AEVMController` now registers those mappings before calling `android51_host_run` on its owned execution thread. Host TCG execution does not validate iOS W^X behavior.

## Tests and outstanding integration

`qemud:gsm` now supplies the reference RIL with a virtual test SIM, operator
registration, radio power state and one IPv4 PDP context over `eth0`. The SDK
init scripts configure 10.0.2.15, gateway 10.0.2.2 and DNS 10.0.2.3; reference-ril
publishes these to Android's connectivity service. The existing
`android.qemud=1` argument previously selected a modem service that was missing.
This is an emulated data network, not the iPhone's SIM or telephony service.
Calls/SMS and unknown AT commands return errors. A virtual registered link is
not proof of Internet access; Android's connectivity checks remain enabled.
QTest covers split commands, queue backpressure, radio off/on and PDP state,
plus a real UDP round trip through SMC91C111 and libslirp to a local host socket.
Real-device DNS/HTTP and Android 6 compatibility still require device testing.

`Tests/QEMU/test_goldfish.py` exercises actual QEMU MMIO/DMA, timer and display IRQs, NAND persistence/read-only protection, descriptor preservation, audio, input capabilities, and split boot-property frames. A separate TCG process executes generated ARM instructions through the board's boot loader and emits a serial marker. No Android images are used, and this marker is not an Android boot result.

`host.c` provides the single-lifecycle run/pause/stop/metrics API. Display/PCM/serial callbacks and input draining connect to the iOS runtime. `adb.c` and `pipe.c` provide a bounded, thread-safe, in-process adbd stream with accept/start negotiation and explicit disconnect recovery. No TCP server is involved. `Tests/QEMU/test_embedded.py` verifies external-arena TCG execution, real pixel/PCM callbacks, bidirectional ADB bytes through guest MMIO, and stopping while paused.

Remaining: actual stock kernel/ramdisk/system compatibility and Launcher boot; legacy serial qemud fallback and other required guest services; macOS cross-build execution and iOS framework/Keychain/JIT validation; real Android APK installation and network traffic; optional GLES translation/ANGLE; iOS performance and memory-pressure validation. Unknown pipe services return errors rather than fake success. The host launcher uses the null audio backend; the iOS bridge receives the consumed PCM and feeds AVAudioEngine. No public ADB or QMP listener is opened.

The AOSP-derived files retain GPL-2.0-only notices; see LICENSE. TCG allocator patches preserve upstream MIT terms (TCG-MIT.txt), the Apple JIT header preserves LGPL-2.1-or-later terms (LGPL-2.1.txt), and the error-report patch preserves GPL-2.0-or-later terms. Corresponding source/build inputs must accompany any eventual QEMU binary distribution.
