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

  # Normalize CRLF to LF for cross-platform compatibility
  sed -i 's/\r$//' "$stdout" "$stderr"

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
  sed -i 's/\r$//' "$stdout" "$stderr"
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

# --- Arrays ---
run_case "arrays_success" "run" 0 $'1 20 []\n' ""
run_case "arrays_type_error" "run" 65 "" $'2:18: error: Array elements must have compatible types.\n'
run_case "arrays_oob" "run" 70 "" $'runtime error: Array index out of bounds.\n'
run_contains_case "arrays_bytecode" "--bytecode" "ArrayNew" "IndexGet" "IndexSet"

# --- Operators ---
run_case "operators_arithmetic" "run" 0 $'13\n7\n30\n3\n1\n-5\n-6\n' ""
run_case "operators_comparison" "run" 0 $'true\ntrue\ntrue\ntrue\ntrue\nfalse\n' ""
run_case "operators_logic" "run" 0 $'true\nfalse\nfalse\ntrue\ntrue\nfalse\nfalse\ntrue\nshort-circuit:\ndone\n' ""

# --- Structs ---
run_case "structs_basic" "run" 0 $'3 4\n10\n' ""

# --- Enums ---
run_case "enums_basic" "run" 0 $'green\nnot red\n' ""

# --- Inspect ---
run_case "inspect_basic" "run" 0 $'zero\none\nother\n' ""

# --- Generics ---
run_case "generics_basic" "run" 0 $'42\nhello\n99\nworld\n1 one\n42\n' ""

# --- Control Flow ---
run_case "control_flow" "run" 0 $'10\n3\n2\n1\nyes\n' $'16:6: warning: Condition is always true.\n'

# --- Functions ---
run_case "functions_recursion" "run" 0 $'720\n55\n' ""

# --- IO Methods ---
run_case "io_methods" "run" 0 $'hello world\n1 + 2 = 3\nno newline here\ndone\n' ""

# --- Warnings ---
run_case "warnings" "run" 0 "" $'4:6: warning: Condition is always true.\n8:9: warning: Condition is always false; loop body never executes.\n9:5: warning: Unused variable \'x\'.\n13:3: warning: Unreachable code.\n2:3: warning: Unused variable \'unused\'.\n'

# --- Array Methods ---
run_case "array_methods" "run" 0 $'len: 5\nafter push: len = 6\npopped: 6\nremoved at 0: 1\ncontains 3: true\ncontains 99: false\nafter clear: len = 0\n' ""
run_case "array_insert" "run" 0 $'1 10 2 3\nlen = 7\n7 8 9\n' ""
run_case "array_slice_reverse" "run" 0 $'index_of 3: 2\nindex_of 99: -1\nslice(1,4): 2 3 4\nreversed: 5 4 3 2 1\n' ""

# --- Chained Comparisons ---
run_case "chained_comparisons" "run" 0 $'in rangenot smalledge' ""

# --- Pipeline Operator ---
run_case "pipeline" "run" 0 $'5 |> twice |> add_one = 11\n3 |> add_one |> twice |> negate = -8\n' ""

# --- First-class native functions ---
run_case "native_fn_firstclass" "run" 0 $'hello from variable\n1 + 2 = 3\nab\npipeline test\n' $'stderr message\n'

# --- Implicit return ---
run_case "implicit_return" "run" 0 $'25\ntrue\nfalse\n42\n' ""

# --- Structured unpacking ---
run_case "unpack_decl" "run" 0 $'10\n20\n[30, 40, 50]\nhello\nworld\n' ""

# --- Guard Statement ---
run_case "guard_stmt" "run" 0 $'5\n-1\n5\n3\n0\n' ""

# --- Error: missing using io ---
run_case "error_missing_using_io" "run" 65 "" $'2:3: error: Module \'io\' is not imported. Add \'using io;\' at the top of the file.\n'

if [[ "$FAILURES" -ne 0 ]]; then
  echo "$FAILURES CLI golden test(s) failed." >&2
  exit 1
fi

echo "CLI golden tests passed."
