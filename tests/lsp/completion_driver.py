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


def _find_lsp_bin():
    """Locate the built kinglet-lsp binary across platforms and build dirs."""
    names = ["kinglet-lsp.exe", "kinglet-lsp"] if os.name == "nt" else ["kinglet-lsp"]
    for build_dir in ("Debug", "Default", "Release"):
        for name in names:
            cand = os.path.join(ROOT, "out", build_dir, name)
            if os.path.exists(cand):
                return cand
    # Fall back to the conventional path for a clear error message.
    return os.path.join(ROOT, "out", "Debug", names[0])


LSP_BIN = _find_lsp_bin()


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

    # self. inside a trait default body must not duplicate methods.
    ("trait_self_dot_dedup",
     "trait Shape {\n"
     "  string name(self);\n"
     "  int area(self);\n"
     "  string describe(self) => self.|;\n"
     "}\n",
     ["name", "area", "describe"],
     []),

    # impl <Type> must complete struct/enum names.
    ("impl_target",
     "struct Rect { int w; int h; }\n"
     "impl R|\n",
     ["Rect"],
     ["int", "void", "if"]),

    # Parameter type position must not offer void/auto.
    ("param_type_no_void_auto",
     "struct Rect { int w; int h; }\n"
     "impl Rect {\n  Rect scale(self, | factor) => self.w;\n}\n",
     ["int", "string", "Rect"],
     ["void", "auto"]),

    # Return type position keeps void/auto (they are valid there).
    ("return_type_keeps_void",
     "struct Rect { int w; int h; }\n"
     "impl Rect {\n  | scale(self) => self.w;\n}\n",
     ["int", "void", "auto"],
     []),

    # A member-access operator at top level is invalid: offer nothing rather
    # than flooding every declaration keyword.
    ("toplevel_dot_empty",
     "struct Rect { int w; }\n.|\n",
     [],
     ["struct", "impl", "import", "fun"]),

    ("toplevel_colon_empty",
     "struct Rect { int w; }\n::|\n",
     [],
     ["struct", "impl", "import", "fun"]),

    # io::out / io::err expose `line`; io::in exposes `secret`.
    ("io_out_dot_line",
     "using io;\nint main() {\n  io::out.|\n}\n",
     ["line"],
     []),

    ("io_in_dot_secret",
     "using io;\nint main() {\n  io::in.|\n}\n",
     ["secret"],
     []),

    # Method-call chains: r.scale(2). resolves through the return type.
    ("method_chain_dot",
     "struct Rect { int w; int h; }\n"
     "impl Rect {\n  Rect scale(self, int f) => self;\n  int area(self) => self.w;\n}\n"
     "int main() {\n  Rect r { 3, 4 };\n  r.scale(2).|\n}\n",
     ["area", "scale", "w", "h"],
     []),

    # impl-body return-type position offers user-defined type names.
    ("impl_return_user_type",
     "struct Rect { int w; int h; }\nimpl Rect {\n  R|\n}\n",
     ["Rect"],
     []),

    # Positional struct-literal slots are value expressions: offer self/locals.
    ("struct_literal_self",
     "struct Rect { int w; int h; }\n"
     "impl Rect {\n  Rect scale(self, int f) => Rect { sel| };\n}\n",
     ["self"],
     [";", "{"]),

    # `using <ns>` offers namespaces only, never declaration keywords, and the
    # partial input must not echo back as a candidate.
    ("using_namespace",
     "using i|\n",
     ["io"],
     ["import", "impl", "using", "i"]),

    ("using_namespace_empty",
     "using |\n",
     ["io"],
     ["import", "struct", "fun", ""]),

    # fs / sys namespaces complete after `using` and expose their members.
    ("using_namespace_fs_sys",
     "using |\n",
     ["io", "fs", "sys"],
     ["import", "struct"]),

    ("namespace_fs",
     "using fs;\nfn main() {\n  fs::|\n}\n",
     ["__read", "__write"],
     ["out", "args"]),

    ("namespace_sys",
     "using sys;\nfn main() {\n  sys::|\n}\n",
     ["args"],
     ["__read", "out"]),
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
