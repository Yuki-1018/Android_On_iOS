#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
xcrun swift test --scratch-path build/swift -Xswiftc -warnings-as-errors
