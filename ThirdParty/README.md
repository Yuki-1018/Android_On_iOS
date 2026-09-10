# Third-party provenance

Exact dependency and reference revisions are recorded in `dependencies.lock.json`. `scripts/fetch_references.py` fetches those commits detached and refuses to overwrite dirty checkouts. Checkouts are excluded from the app and repository. No guest images are fetched.

| Source | Revision | License | Use in this repository |
|---|---|---|---|
| [utmapp/qemu](https://github.com/utmapp/qemu) | `b44153a4b6aabf86edebf92199b14aec26e15d59` | GPL-2.0 and per-file terms | Base for the host Android51 engine, with reproducible overlay and patches. Built as the embedded iOS engine by the cross-build pipeline; iOS build execution is not yet verified. |
| [utmapp/UTM](https://github.com/utmapp/UTM) | `b6f7475be54f9cb542c46b131319454b83489ced` | Apache-2.0 application, separate dependency licenses | Architecture reference and pinned cross-build machinery, specialized to minimal dependencies. No UI copied. |
| [StikDebug/StikJIT](https://github.com/StikDebug/StikJIT) | `3623e725876f76aecb0520582ad6194bacb15d39` | MPL-2.0 | Two app-side universal entry points adapted in `StikJITProtocol/UniversalProtocol.c`; memory-map feature detection ported in `JIT/Environment.mm`. Framework and debugger scripts are not bundled. |
| [AOSP emulator](https://android.googlesource.com/platform/external/qemu/+/006a174244e336c578f45bf78a9dbdbd890c217d/) | `006a174244e336c578f45bf78a9dbdbd890c217d` (`android-5.1.1_r38`) | GPL-2.0 and per-file terms | Goldfish device implementations ported into `AndroidQemuCompat/qemu/`; original per-file notices retained. |
| [AOSP libsparse format](https://android.googlesource.com/platform/system/core/+/605f8706c88b2cd5d024b0a6b7253a78d968ba72/libsparse/sparse_format.h) | `605f8706c88b2cd5d024b0a6b7253a78d968ba72` | Apache-2.0 | Binary format reference for independent streaming C++ parser; no code copied. |

The source for StikJIT adaptations is included, with the full MPL-2.0 license in `StikJITProtocol/LICENSE`. The AOSP-derived Goldfish overlay is GPL-2.0-only, with the full license in `AndroidQemuCompat/LICENSE`. Other new project implementation is GPL-2.0-or-later, see the root LICENSE.

`AndroidQemuCompat/` contains the Goldfish board port and TCG patches. Its README describes supported devices and remaining integration. A dependency pin or device test is not evidence of Android Launcher compatibility.
