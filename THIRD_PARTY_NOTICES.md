# Third-party notices

New project code is made available under GPL-2.0-or-later; see LICENSE. Individual third-party-derived files retain their own terms.

## StikDebug / StikJIT

Source: https://github.com/StikDebug/StikJIT

Revision: `3623e725876f76aecb0520582ad6194bacb15d39`

License: Mozilla Public License 2.0, reproduced in `ThirdParty/StikJITProtocol/LICENSE`.

`ThirdParty/StikJITProtocol/UniversalProtocol.c` adapts the two arm64 app-side breakpoint protocol functions from `INTEGRATION.md`. The change isolates them in an arm64-only C translation unit. `JIT/Environment.mm` adapts the memory-map property-key detection from `Sources/ProcessInfo+TXM.swift` into Objective-C++, dynamically loads SPI, and also checks SPTM. The original referenced snippets contain no copyright header; origin and MPL terms are recorded in the adapted files. The StikJIT framework, helper extension, pairing data, idevice libraries and JavaScript debugger scripts are not included.

## AOSP Goldfish and UTM QEMU

`ThirdParty/AndroidQemuCompat/qemu/` ports the ARM board and Goldfish devices from AOSP external/qemu revision `006a174244e336c578f45bf78a9dbdbd890c217d` (Android 5.1.1 r38) to UTM QEMU revision `b44153a4b6aabf86edebf92199b14aec26e15d59`. The AOSP copyright and GPL version 2 notices are retained; the GPL text is in `ThirdParty/AndroidQemuCompat/LICENSE`. Changes include modern QEMU memory/block/audio APIs, bounded DMA restricted to RAM, fixed device topology, Protocol B capabilities, dirty framebuffer updates, and bounded pipe framing. See the directory README for omissions.

The patches adapt UTM QEMU's TCG region allocator to external RW/RX mappings, disable APRR switching for the iOS split-alias path, fix compiler warnings, and prevent executing cross-built iOS binaries on macOS during configuration. The configure patch retains GPL-2.0-or-later terms. Patched upstream files retain their original licenses: `tcg/region.c` MIT, `util/error-report.c` GPL-2.0-or-later, `include/tcg/tcg-apple-jit.h` LGPL-2.1-or-later. Full upstream source/licenses are fetched at the pinned revision for a host build; host binaries are not packaged by this repository's CI. The iOS packaging pipeline now requires the QEMU framework and its actual dependency closure. A successful iOS build has not yet been verified in this environment.

## UTM dependency build machinery

`scripts/prepare_ios_sysroot.py` specializes the build script at UTM revision `b6f7475be54f9cb542c46b131319454b83489ced`. The generated script retains its ISC notice (Angelo Haller, 2014), source URLs and applicable upstream patches. Only libffi, libiconv, gettext, GLib (and its wrap dependencies), pixman, libslirp libucontext and the pinned WebKit ANGLE EGL/GLES implementation are selected. UTM UI, SPICE, Vulkan, Hypervisor and guest images are not built by this specialization. Per-dependency licenses are copied into the app; actual prepared engine/dependency sources and scripts accompany the IPA in a separate source artifact. These components retain their upstream licenses.

The ADB accept/start protocol was checked against AOSP `android/adb-qemud.c` at the pinned AOSP revision. The new bounded in-process transport and classic ADB client are project code under the root GPL-2.0-or-later license. No ADB server executable is bundled.

The data modem's static test SIM records in `gsm_sim.inc` are adapted from
`telephony/sim_card.c` at the same pinned AOSP revision, under GPL-2.0-only with
the original notice retained. AT/data-call behavior was checked against AOSP
`hardware/ril/reference-ril/reference-ril.c`, tag `android-5.1.1_r38`.

## Other references

UTM (Apache-2.0 application), UTM QEMU (GPL and per-file licenses), AOSP libsparse (Apache-2.0), AOSP Android input layouts and Linux input documentation were consulted for architecture or formats. Exact selected revisions and integration status are listed in `ThirdParty/README.md`. Their licensing is not replaced by the license of the new app.

The root GPL text is the standard GNU GPL version 2 text also distributed as QEMU's COPYING. The engine packaging scripts include corresponding source/build inputs for the actual revision and modifications; a URL to a moving upstream branch is not a substitute.

Android OS/kernel images, proprietary Google software and user APKs are not included in this repository or its intended artifacts.

## AOSP EmuGL and ANGLE

`ThirdParty/EmuGL/upstream` embeds the GLES1/GLES2/renderControl decoders and framebuffer implementation from AOSP external/qemu commit `e6aef36e024c3265ff8103f8d2265dd235851ef4`, directory `distrib/android-emugl`. Original file notices remain; Apache-2.0 text is in `ThirdParty/EmuGL/LICENSE-APACHE-2.0`. Adaptations and source URLs are recorded in `ThirdParty/EmuGL/UPSTREAM.md`.

The iOS EGL/GLES backend is ANGLE from UTM WebKit commit `ed78ab6e1a37f4f11583a0bd038f22ec91f3ff10`. ANGLE and its component licenses retain their original terms. The build includes those sources in the corresponding-source artifact and copies dependency license texts into the app. The desktop EmuGL GL translators, native windows and TCP server are not built into the iOS renderer.
