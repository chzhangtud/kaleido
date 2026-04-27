#!/usr/bin/env python3
import json
import sys
from typing import Any


def normalize(data: Any) -> Any:
    if isinstance(data, dict):
        out = {}
        for key in sorted(data.keys()):
            if key in ("frameIndex",):
                continue
            out[key] = normalize(data[key])
        return out
    if isinstance(data, list):
        normalized = [normalize(x) for x in data]
        if normalized and isinstance(normalized[0], dict):
            if "id" in normalized[0]:
                return sorted(normalized, key=lambda x: x.get("id", ""))
            if "from" in normalized[0] and "to" in normalized[0]:
                return sorted(normalized, key=lambda x: (x.get("from", ""), x.get("to", ""), x.get("type", ""), x.get("state", "")))
        return normalized
    return data


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: compare_rendergraph_json.py <expected.json> <actual.json>")
        return 2

    with open(sys.argv[1], "r", encoding="utf-8-sig") as f:
        expected = normalize(json.load(f))
    with open(sys.argv[2], "r", encoding="utf-8-sig") as f:
        actual = normalize(json.load(f))

    if expected == actual:
        print("rendergraph json compare: PASS")
        return 0
    print("rendergraph json compare: FAIL")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
