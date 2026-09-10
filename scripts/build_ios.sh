#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk_version="$(xcrun --sdk iphoneos --show-sdk-version)"
if [[ "${sdk_version%%.*}" -lt 26 ]]; then
  echo "iPhoneOS SDK 26 or later is required" >&2
  exit 1
fi
[[ -f build/ios-frameworks/AndroidQEMU.framework/AndroidQEMU ]] || { echo "Build iOS engine dependencies first" >&2; exit 1; }
python3 scripts/generate_project.py
xcodebuild -project AndroidEmu.xcodeproj -scheme AndroidEmu -configuration Release \
  -sdk iphoneos -destination 'generic/platform=iOS' -derivedDataPath build/DerivedData \
  CODE_SIGNING_ALLOWED=NO CODE_SIGNING_REQUIRED=NO CODE_SIGN_IDENTITY= build

python3 scripts/embed_engine.py build/DerivedData/Build/Products/Release-iphoneos/AndroidEmu.app
