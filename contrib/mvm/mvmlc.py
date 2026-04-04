#!/usr/bin/env python3
"""mvmlc: compile .mvc source into MVM bytecode hex.

Instruction format:
  OPCODE [arg] [k=v ...]
Examples:
  PUSH 2
  LOAD key=balance
  STORE key=balance
  CREATE_CONTRACT contract_id=myc code_hash=0xabc creator=alice
  CREATE_TOKEN contract_id=myc symbol=USDT name=Tether owner=issuer supply=1000000 decimals=6
  TRANSFER_TOKEN symbol=USDT from=issuer to=alice amount=100
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Dict, List

# Bytecode op map shared with mvm_runtime decoder.
OPCODE: Dict[str, int] = {
    "PUSH": 0x01,
    "LOAD": 0x02,
    "STORE": 0x03,
    "ADD": 0x04,
    "SUB": 0x05,
    "MUL": 0x06,
    "DIV": 0x07,
    "EQ": 0x08,
    "GT": 0x09,
    "LT": 0x0A,
    "DUP": 0x0B,
    "SWAP": 0x0C,
    "JUMP": 0x0D,
    "JUMPI": 0x0E,
    "BLOCK_HEIGHT": 0x0F,
    "CREATE_CONTRACT": 0x10,
    "CREATE_TOKEN": 0x11,
    "MINT_TOKEN": 0x12,
    "TRANSFER_TOKEN": 0x13,
    "ASSERT": 0x14,
    "EMIT": 0x15,
    "HALT": 0xFF,
}

NO_ARG = {"ADD", "SUB", "MUL", "DIV", "EQ", "GT", "LT", "DUP", "SWAP", "BLOCK_HEIGHT", "HALT"}


def encode_str(value: str) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) > 255:
        raise ValueError("string argument too long (>255 bytes)")
    return bytes([len(raw)]) + raw


def parse_kv(parts: List[str]) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for p in parts:
        if "=" not in p:
            raise ValueError(f"expected key=value argument, got '{p}'")
        k, v = p.split("=", 1)
        out[k] = v
    return out


def compile_mvc(text: str) -> bytes:
    out = bytearray()
    for lineno, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        op = parts[0].upper()
        if op not in OPCODE:
            raise ValueError(f"line {lineno}: unsupported opcode '{op}'")
        out.append(OPCODE[op])

        if op == "PUSH":
            if len(parts) != 2:
                raise ValueError(f"line {lineno}: PUSH requires one integer argument")
            v = int(parts[1], 10)
            if v < 0 or v > 0xFFFFFFFFFFFFFFFF:
                raise ValueError(f"line {lineno}: PUSH out of u64 range")
            out.extend(v.to_bytes(8, "little", signed=False))
            continue

        if op in NO_ARG:
            if len(parts) != 1:
                raise ValueError(f"line {lineno}: {op} takes no arguments")
            continue

        if op in {"LOAD", "STORE"}:
            kv = parse_kv(parts[1:])
            key = kv.get("key", "")
            if not key:
                raise ValueError(f"line {lineno}: {op} requires key=<name>")
            out.extend(encode_str(key))
            continue

        if op in {"JUMP", "JUMPI"}:
            kv = parse_kv(parts[1:])
            target = int(kv.get("target", "-1"), 10)
            if target < 0 or target > 0xFFFF:
                raise ValueError(f"line {lineno}: invalid jump target")
            out.extend(target.to_bytes(2, "little", signed=False))
            continue

        if op == "ASSERT":
            kv = parse_kv(parts[1:])
            out.extend(encode_str(kv.get("message", "assertion failed")))
            continue

        if op == "EMIT":
            kv = parse_kv(parts[1:])
            out.extend(encode_str(kv.get("name", "event")))
            pop = kv.get("pop", "1")
            out.append(1 if pop not in {"0", "false", "False"} else 0)
            continue

        if op == "CREATE_CONTRACT":
            kv = parse_kv(parts[1:])
            out.extend(encode_str(kv.get("contract_id", "")))
            out.extend(encode_str(kv.get("code_hash", "")))
            out.extend(encode_str(kv.get("creator", "unknown")))
            continue

        if op == "CREATE_TOKEN":
            kv = parse_kv(parts[1:])
            out.extend(encode_str(kv.get("contract_id", "")))
            out.extend(encode_str(kv.get("symbol", "")))
            out.extend(encode_str(kv.get("name", "")))
            out.extend(encode_str(kv.get("owner", "owner")))
            supply = int(kv.get("supply", "0"), 10)
            decimals = int(kv.get("decimals", "18"), 10)
            out.extend(supply.to_bytes(8, "little", signed=False))
            out.append(decimals & 0xFF)
            continue

        if op == "MINT_TOKEN":
            kv = parse_kv(parts[1:])
            out.extend(encode_str(kv.get("symbol", "")))
            out.extend(encode_str(kv.get("to", "")))
            amount = int(kv.get("amount", "0"), 10)
            out.extend(amount.to_bytes(8, "little", signed=False))
            continue

        if op == "TRANSFER_TOKEN":
            kv = parse_kv(parts[1:])
            out.extend(encode_str(kv.get("symbol", "")))
            out.extend(encode_str(kv.get("from", "")))
            out.extend(encode_str(kv.get("to", "")))
            amount = int(kv.get("amount", "0"), 10)
            out.extend(amount.to_bytes(8, "little", signed=False))
            continue

        raise ValueError(f"line {lineno}: unhandled opcode '{op}'")

    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser(description="Compile .mvc source to MVM bytecode hex")
    parser.add_argument("--input", required=True, help="Path to .mvc file")
    parser.add_argument("--output", default="", help="Optional output file for hex bytecode")
    args = parser.parse_args()

    src = Path(args.input).read_text(encoding="utf-8")
    bytecode = compile_mvc(src).hex()

    if args.output:
        Path(args.output).write_text(bytecode + "\n", encoding="utf-8")
    else:
        print(bytecode)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
