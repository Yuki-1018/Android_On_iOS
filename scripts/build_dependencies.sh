#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Build the portable core and the actual embedded Goldfish engine.
cmake -S . -B build/ios-core -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 -DEMU_BUILD_TESTS=OFF \
  -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build build/ios-core --config Release

bash scripts/build_qemu_ios.sh
