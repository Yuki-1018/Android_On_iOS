#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source_dir="${1:-ThirdParty/checkouts/qemu}"
source_dir="$(cd "$source_dir" && pwd)"
python3 scripts/prepare_qemu.py "$source_dir"
if [[ "${ANDROID51_GPU:-0}" == 1 ]]; then
  cmake -S ThirdParty/EmuGL -B build/emugl-host -G Ninja -DEMUGL_TESTS=ON \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="$PWD/build/emugl-prefix"
  cmake --build build/emugl-host --parallel 2
  cmake --install build/emugl-host
  export PKG_CONFIG_PATH="$PWD/build/emugl-prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
fi
build_dir="${QEMU_BUILD_DIR:-build/qemu-host}"
mkdir -p "$build_dir"
cd "$build_dir"
"$source_dir/configure" --target-list=arm-softmmu --without-default-devices \
  --disable-docs --disable-gtk --disable-sdl --disable-vnc --disable-spice \
  --disable-opengl --disable-virglrenderer --disable-guest-agent --disable-tools \
  --disable-user --enable-werror --enable-slirp --enable-pixman --enable-shared-lib -Db_staticpic=true
ninja -j "${QEMU_BUILD_JOBS:-2}" qemu-system-arm
