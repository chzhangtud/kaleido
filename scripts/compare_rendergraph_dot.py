#!/usr/bin/env python3
import sys


def normalize(text: str) -> str:
    lines = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        lines.append(line)
    return "\n".join(sorted(lines))


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: compare_rendergraph_dot.py <expected.dot> <actual.dot>")
        return 2
    with open(sys.argv[1], "r", encoding="utf-8") as f:
        expected = normalize(f.read())
    with open(sys.argv[2], "r", encoding="utf-8") as f:
        actual = normalize(f.read())
    if expected == actual:
        print("rendergraph dot compare: PASS")
        return 0
    print("rendergraph dot compare: FAIL")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
