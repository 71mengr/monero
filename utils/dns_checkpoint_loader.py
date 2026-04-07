#!/usr/bin/env python3
"""
Load checkpoints from a dns.txt endpoint and query by block height.

Expected dns.txt line format examples:
  3250000,0f40...abcd
  3250000:0f40...abcd
  3250000 0f40...abcd

Blank lines and lines starting with '#' are ignored.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import sys
import urllib.error
import urllib.request
from typing import Dict


LINE_RE = re.compile(r"^\s*(\d+)\s*[,:\s]\s*([0-9a-fA-F]{64})\s*$")


def read_text(source: str, timeout: float) -> str:
    if source.startswith(("http://", "https://")):
        with urllib.request.urlopen(source, timeout=timeout) as response:
            return response.read().decode("utf-8")
    return pathlib.Path(source).read_text(encoding="utf-8")


def parse_checkpoints(text: str) -> Dict[int, str]:
    checkpoints: Dict[int, str] = {}
    for index, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        match = LINE_RE.match(line)
        if not match:
            raise ValueError(f"invalid line {index}: {raw_line}")

        height = int(match.group(1))
        block_hash = match.group(2).lower()
        checkpoints[height] = block_hash

    return checkpoints


def write_download(text: str, output_path: str) -> None:
    path = pathlib.Path(output_path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def print_result(checkpoints: Dict[int, str], block_height: int | None, as_json: bool) -> int:
    if block_height is None:
        if as_json:
            print(json.dumps(checkpoints, indent=2, sort_keys=True))
        else:
            for height in sorted(checkpoints):
                print(f"{height},{checkpoints[height]}")
        return 0

    block_hash = checkpoints.get(block_height)
    if block_hash is None:
        print(f"height {block_height} not found", file=sys.stderr)
        return 2

    if as_json:
        print(json.dumps({"height": block_height, "hash": block_hash}, indent=2))
    else:
        print(f"{block_height},{block_hash}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Load dns.txt checkpoints from URL or file and query block hash by height."
    )
    parser.add_argument(
        "--source",
        default="http://127.0.0.1/dns.txt",
        help="dns.txt URL or local file path (default: %(default)s)",
    )
    parser.add_argument(
        "--height",
        type=int,
        help="block height to query. If omitted, prints all checkpoints.",
    )
    parser.add_argument(
        "--download-out",
        help="optional path to save the downloaded dns.txt content",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="network timeout in seconds for URL source (default: %(default)s)",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="print output as JSON",
    )
    args = parser.parse_args()

    try:
        text = read_text(args.source, args.timeout)
        checkpoints = parse_checkpoints(text)
    except (urllib.error.URLError, TimeoutError) as error:
        print(f"failed to download source '{args.source}': {error}", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"failed to read source '{args.source}': {error}", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"failed to parse checkpoints: {error}", file=sys.stderr)
        return 1

    if args.download_out:
        try:
            write_download(text, args.download_out)
            digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
            print(f"saved dns.txt to {args.download_out} (sha256={digest})", file=sys.stderr)
        except OSError as error:
            print(f"failed to save '{args.download_out}': {error}", file=sys.stderr)
            return 1

    return print_result(checkpoints, args.height, args.json)


if __name__ == "__main__":
    raise SystemExit(main())
