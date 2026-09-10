#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
source_dir="${1:-ThirdParty/checkouts/qemu}"
source_dir="$(cd "$source_dir" && pwd)"
python3 scripts/prepare_qemu.py "$source_dir"
mkdir -p build/qemu-host
cd build/qemu-host
"$source_dir/configure" --target-list=arm-softmmu --without-default-devices \
  --disable-docs --disable-gtk --disable-sdl --disable-vnc --disable-spice \
  --disable-opengl --disable-virglrenderer --disable-guest-agent --disable-tools \
  --disable-user --enable-werror --enable-slirp --enable-pixman --enable-shared-lib -Db_staticpic=true
ninja -j "${QEMU_BUILD_JOBS:-2}" qemu-system-arm
