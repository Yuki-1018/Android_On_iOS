# Embedded Goldfish renderer

The `upstream/` directory is a build-input subset derived from Android Open Source Project EmuGL:

- Repository: https://android.googlesource.com/platform/external/qemu
- Commit: `e6aef36e024c3265ff8103f8d2265dd235851ef4`
- Directory: `distrib/android-emugl`
- Archive: https://android.googlesource.com/platform/external/qemu/+archive/e6aef36e024c3265ff8103f8d2265dd235851ef4/distrib/android-emugl.tar.gz
- Individual files retain their upstream copyright/license notices (principally Apache-2.0).

AndroidEmu modifications:

- CMake build, native emugen for iOS cross-compilation, direct EGL/GLES dispatch to ANGLE Metal.
- No desktop translator/window/server: bounded socketpairs connect the Goldfish pipe directly to per-client RenderThread instances.
- FrameBuffer initializes its texture blitter without a desktop window and posts gralloc color buffers through a callback. Matching shader varying precision is required by real GLES backends.
- GLES2 capabilities are checked using a real context. Configs for unavailable GLES1 contexts are removed. Guest version/extensions are limited to commands supported by the legacy protocol.
- Streaming packet size limits, unaligned wire loads, and 64-bit double unpacking corrected. Large idle staging buffers shrink.
- Rendering connections do not hold a global lock across blocking replies. FrameBuffer's shared-resource locks and atomic references remain.
- CPU presentation coalesces changes and only copies modified rows. GL-to-gralloc orientation is applied once.

The iOS backend is UTM's pinned WebKit ANGLE, built by the upstream `build_angle` function:

- https://github.com/utmapp/WebKit/tree/ed78ab6e1a37f4f11583a0bd038f22ec91f3ff10/Source/ThirdParty/ANGLE
- ANGLE includes GLES1 emulation in its frontend; `Display.cpp` adds GLES1 renderable configurations to the Metal backend's GLES2 configurations.
- `scripts/normalize_angle.py` preserves the EGL → GLES dylib dependency for framework packaging. Corresponding ANGLE sources/licenses are collected with the dependency build inputs.

Android 6 reference protocols:

- https://android.googlesource.com/device/generic/goldfish/+/android-6.0.1_r1/opengl/system/OpenglSystemCommon/HostConnection.cpp
- https://android.googlesource.com/device/generic/goldfish/+/android-6.0.1_r1/opengl/system/renderControl_enc/renderControl.in

The guest sends a zero 32-bit client-flags word after `pipe:opengles\0`, followed by GLES1, GLES2 and renderControl messages on the same stream. The renderControl operations used by Android 6 are present in this revision. This does not add GLES3, Vulkan, ARM64 or newer Android guest support.
