#!/usr/bin/env python3
"""Drive kinglet-lsp over JSON-RPC and assert completion labels.

Each test case opens an in-memory document, places the cursor at the `|`
marker, requests textDocument/completion, and checks that the returned
labels include (and optionally exclude) expected entries.

The `|` marker denotes the cursor; it is stripped from the source before
the document is sent. Line/character are 0-based, matching LSP.
"""

import json
import subprocess
import sys
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LSP_BIN = os.path.join(ROOT, "out", "Debug", "kinglet-lsp")


def frame(msg: dict) -> bytes:
    body = json.dumps(msg).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("utf-8") + body


def read_message(stream) -> dict:
    length = None
    while True:
        line = stream.readline()
        if not line:
            return None
        line = line.decode("utf-8").strip()
        if line.lower().startswith("content-length:"):
            length = int(line.split(":", 1)[1].strip())
        elif line == "":
            break
    if length is None:
        return None
    body = stream.read(length)
    return json.loads(body.decode("utf-8"))


def split_cursor(src: str):
    """Return (clean_source, line, character) from a `|`-marked source."""
    idx = src.index("|")
    before = src[:idx]
    line = before.count("\n")
    character = len(before) - (before.rfind("\n") + 1)
    return src[:idx] + src[idx + 1:], line, character


def request_completion(uri: str, source: str, line: int, character: int):
    """Run a full LSP session for one completion request; return labels."""
    proc = subprocess.Popen(
        [LSP_BIN],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    out = []
    try:
        proc.stdin.write(frame({
            "jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {},
        }))
        proc.stdin.write(frame({
            "jsonrpc": "2.0", "method": "textDocument/didOpen",
            "params": {"textDocument": {"uri": uri, "languageId": "kinglet",
                                        "version": 1, "text": source}},
        }))
        proc.stdin.write(frame({
            "jsonrpc": "2.0", "id": 2, "method": "textDocument/completion",
            "params": {"textDocument": {"uri": uri},
                       "position": {"line": line, "character": character}},
        }))
        proc.stdin.write(frame({
            "jsonrpc": "2.0", "id": 3, "method": "shutdown", "params": {},
        }))
        proc.stdin.write(frame({"jsonrpc": "2.0", "method": "exit"}))
        proc.stdin.flush()

        while True:
            msg = read_message(proc.stdout)
            if msg is None:
                break
            if msg.get("id") == 2:
                result = msg.get("result", {})
                items = result.get("items", []) if isinstance(result, dict) else result
                out = [it.get("label", "") for it in items]
                break
    finally:
        proc.stdin.close()
        proc.wait(timeout=5)
    return out


# (case_name, source_with_cursor, must_include, must_exclude)
CASES = [
    ("top_level_decl",
     "|\n",
     ["struct", "enum", "trait", "impl", "import", "using"],
     ["if", "return", "break"]),

    ("statement_keywords",
     'fn main() {\n  |\n}\n',
     ["if", "for", "while", "let", "return"],
     ["struct", "trait"]),

    ("field_access",
     "struct Rect { int w; int h; }\n"
     "fn area(Rect r) {\n  r.|\n}\n",
     ["w", "h"],
     ["struct", "if"]),

    ("namespace_io",
     "using io;\nfn main() {\n  io::|\n}\n",
     ["out", "err", "in"],
     []),

    ("enum_namespace_access",
     "enum Color { Red, Green, Blue }\n"
     "fn main() {\n  Color::|\n}\n",
     ["Red", "Green", "Blue"],
     []),

    ("struct_literal",
     "struct Point { int x; int y; }\n"
     "fn main() {\n  Point p = Point { | }\n}\n",
     ["x", "y"],
     []),

    ("match_arm_enum_variant",
     "enum Color { Red, Green, Blue }\n"
     "fn describe(Color c) {\n  c match {\n    |\n  };\n}\n",
     ["Color::Red", "Color::Green", "Color::Blue"],
     []),
]


def main():
    if not os.path.exists(LSP_BIN):
        print(f"FAIL: lsp binary not found at {LSP_BIN}", file=sys.stderr)
        return 1

    failures = 0
    for name, marked, includes, excludes in CASES:
        source, line, char = split_cursor(marked)
        uri = f"file://{ROOT}/tests/lsp/__{name}.kl"
        try:
            labels = request_completion(uri, source, line, char)
        except Exception as exc:  # noqa: BLE001
            print(f"FAIL: {name}: driver error: {exc}", file=sys.stderr)
            failures += 1
            continue

        label_set = set(labels)
        missing = [x for x in includes if x not in label_set]
        present = [x for x in excludes if x in label_set]
        if missing or present:
            failures += 1
            print(f"FAIL: {name}", file=sys.stderr)
            if missing:
                print(f"  missing: {missing}", file=sys.stderr)
            if present:
                print(f"  unexpected: {present}", file=sys.stderr)
            print(f"  got: {sorted(label_set)}", file=sys.stderr)
        else:
            print(f"PASS: {name}")

    if failures:
        print(f"{failures} LSP completion test(s) failed.", file=sys.stderr)
        return 1
    print("LSP completion tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
