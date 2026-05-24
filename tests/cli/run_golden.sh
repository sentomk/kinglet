#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
KINGLET="$ROOT/out/Debug/kinglet"
TMP_DIR="$(mktemp -d)"
FAILURES=0

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

fail() {
  echo "FAIL: $1" >&2
  FAILURES=$((FAILURES + 1))
}

run_case() {
  local name="$1"
  local mode="$2"
  local expected_exit="$3"
  local expected_stdout="$4"
  local expected_stderr="$5"
  local source="$ROOT/tests/cli/cases/$name.kl"
  local stdout="$TMP_DIR/$name.stdout"
  local stderr="$TMP_DIR/$name.stderr"

  if [[ "$mode" == "run" ]]; then
    "$KINGLET" "$source" >"$stdout" 2>"$stderr"
  else
    "$KINGLET" "$mode" "$source" >"$stdout" 2>"$stderr"
  fi
  local actual_exit=$?

  if [[ "$actual_exit" -ne "$expected_exit" ]]; then
    fail "$name exit: expected $expected_exit, got $actual_exit"
  fi
  if ! diff -u <(printf "%s" "$expected_stdout") "$stdout" >/dev/null; then
    echo "stdout mismatch for $name:" >&2
    diff -u <(printf "%s" "$expected_stdout") "$stdout" >&2
    FAILURES=$((FAILURES + 1))
  fi
  if ! diff -u <(printf "%s" "$expected_stderr") "$stderr" >/dev/null; then
    echo "stderr mismatch for $name:" >&2
    diff -u <(printf "%s" "$expected_stderr") "$stderr" >&2
    FAILURES=$((FAILURES + 1))
  fi
}

run_contains_case() {
  local name="$1"
  local mode="$2"
  shift 2
  local source="$ROOT/tests/cli/cases/$name.kl"
  local stdout="$TMP_DIR/$name.stdout"
  local stderr="$TMP_DIR/$name.stderr"

  "$KINGLET" "$mode" "$source" >"$stdout" 2>"$stderr"
  local actual_exit=$?
  if [[ "$actual_exit" -ne 0 ]]; then
    fail "$name exit: expected 0, got $actual_exit"
  fi
  if [[ -s "$stderr" ]]; then
    echo "unexpected stderr for $name:" >&2
    cat "$stderr" >&2
    FAILURES=$((FAILURES + 1))
  fi
  for expected in "$@"; do
    if ! grep -q "$expected" "$stdout"; then
      fail "$name stdout missing '$expected'"
    fi
  done
}

cd "$ROOT" || exit 1
ninja -C out/Debug >/dev/null

run_case "arrays_success" "run" 0 $'1 20 []\n' ""
run_case "arrays_type_error" "run" 65 "" $'2:18: error: Array elements must have compatible types.\n'
run_case "arrays_oob" "run" 70 "" $'runtime error: Array index out of bounds.\n'
run_contains_case "arrays_bytecode" "--bytecode" "ArrayNew" "IndexGet" "IndexSet"

if [[ "$FAILURES" -ne 0 ]]; then
  echo "$FAILURES CLI golden test(s) failed." >&2
  exit 1
fi

echo "CLI golden tests passed."
