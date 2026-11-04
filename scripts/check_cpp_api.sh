#!/usr/bin/env bash
# Compile-check the public hot-reload C++ API without linking the engine.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXX_BIN="${CXX:-c++}"
"${CXX_BIN}" -std=c++20 -fsyntax-only \
    -I"${ROOT}/engine/include" \
    "${ROOT}/tests/cpp_api_smoke.cpp"
echo "C++ API smoke test passed."

if [[ -x "${ROOT}/build/debug/bin/GaleshiScriptWorker" ]]; then
    python3 "${ROOT}/tests/cpp_worker_roundtrip.py"
fi
