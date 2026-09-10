#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 scripts/generate_project.py
if [[ "${1:-}" == "--references" ]]; then
  python3 scripts/fetch_references.py
fi
echo 'App project generated. --references fetches pinned research sources only; no Android images are downloaded.'
