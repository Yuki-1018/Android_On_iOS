#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
app="${1:-build/DerivedData/Build/Products/Release-iphoneos/AndroidEmu.app}"
[[ -f "$app/AndroidEmu" && -f "$app/Info.plist" ]] || { echo "Build AndroidEmu.app first" >&2; exit 1; }
xcrun lipo -verify_arch arm64 "$app/AndroidEmu"
if codesign --verify "$app" 2>/dev/null; then
  echo "Expected an unsigned app; refusing to package signed content" >&2
  exit 1
fi
mkdir -p build/artifacts
staging="$(mktemp -d "${TMPDIR:-/tmp}/androidemu-package.XXXXXX")"
trap 'rm -rf "$staging"' EXIT
mkdir "$staging/Payload"
ditto "$app" "$staging/Payload/AndroidEmu.app"
ditto -c -k --keepParent "$staging/Payload" build/artifacts/AndroidEmu-development-unsigned.ipa
cp AndroidEmu.entitlements build/artifacts/AndroidEmu.entitlements
cp scripts/signing-notes.txt build/artifacts/signing-notes.txt
python3 scripts/verify_artifact.py build/artifacts/AndroidEmu-development-unsigned.ipa
