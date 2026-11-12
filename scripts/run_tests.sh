#!/usr/bin/env bash
# Native build, style, UI tests, gateway probes and the desktop parity contract.
# The desktop app's component tests are run by parity/check_parity.py.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
cmake --build build --target clean >/dev/null
cmake --build build -j8 >/dev/null

clang-format --dry-run --Werror -style=Google src/*.cc src/*.h tests/*.cc
/tmp/cpplint-venv/bin/cpplint --root=. --recursive src tests >/dev/null

QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

QT_QPA_PLATFORM=offscreen ./build/hermes_ui --probe 2>&1 | grep -q "profiles=6"
QT_QPA_PLATFORM=offscreen ./build/hermes_ui --probe-ws 2>&1 | grep -q "gateway.ready"

python3 parity/check_parity.py

if [[ "${1:-}" == "--with-e2e" ]]; then
  cd "$HOME/.hermes/hermes-agent/apps/desktop"
  npx playwright test e2e/parity-preferences.spec.ts --reporter=line --workers=1
fi

echo "run_tests: PASS"
