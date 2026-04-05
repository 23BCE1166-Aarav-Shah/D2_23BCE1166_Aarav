#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/integration}"
FIXTURE_DIR="${FIXTURE_DIR:-$ROOT_DIR/build/integration-fixture}"
RUNNER="$BUILD_DIR/duplicate_scan_integration_runner"
PERF_LIMIT_MS="${PERF_LIMIT_MS:-20000}"

"$ROOT_DIR/scripts/integration/generate_scan_fixture.sh" "$FIXTURE_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD_DIR" --target duplicate_scan_integration_runner --parallel >/dev/null

OUTPUT_FILE="$BUILD_DIR/scan-output.txt"
"$RUNNER" "$FIXTURE_DIR" | tee "$OUTPUT_FILE"

SUMMARY_LINE="$(grep '^SUMMARY ' "$OUTPUT_FILE")"
if [[ -z "$SUMMARY_LINE" ]]; then
  echo "Missing SUMMARY line from integration runner." >&2
  exit 1
fi

DURATION_MS="$(sed -n 's/.*duration_ms=\([0-9][0-9]*\).*/\1/p' <<<"$SUMMARY_LINE")"
PROGRESS_VALUE="$(sed -n 's/.*progress=\([0-9][0-9]*\).*/\1/p' <<<"$SUMMARY_LINE")"
GROUP_COUNT="$(sed -n 's/.*groups=\([0-9][0-9]*\).*/\1/p' <<<"$SUMMARY_LINE")"

if [[ "$PROGRESS_VALUE" != "100" ]]; then
  echo "Scan did not report 100% progress." >&2
  exit 1
fi

if (( DURATION_MS > PERF_LIMIT_MS )); then
  echo "Performance threshold exceeded: ${DURATION_MS}ms > ${PERF_LIMIT_MS}ms" >&2
  exit 1
fi

if (( GROUP_COUNT < 4 )); then
  echo "Expected at least 4 duplicate groups, found $GROUP_COUNT" >&2
  exit 1
fi

grep -q '^GROUP type=binary ' "$OUTPUT_FILE" || {
  echo "Missing binary duplicate group." >&2
  exit 1
}

grep -q '^GROUP type=image ' "$OUTPUT_FILE" || {
  echo "Missing image duplicate group." >&2
  exit 1
}

grep -q '^GROUP type=video ' "$OUTPUT_FILE" || {
  echo "Missing video duplicate group." >&2
  exit 1
}

grep -q '^GROUP type=audio ' "$OUTPUT_FILE" || {
  echo "Missing audio duplicate group." >&2
  exit 1
}

echo "Integration test passed in ${DURATION_MS}ms with ${GROUP_COUNT} groups."
