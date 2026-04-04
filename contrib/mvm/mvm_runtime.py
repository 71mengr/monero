#!/usr/bin/env python3
"""
MVM (Monero Virtual Machine)
A lightweight deterministic VM for Monero contract logic simulation.

NOTE: MVM executes off-chain and is designed to drive Monero-native actions
(multisig/timelock flows) rather than on-chain bytecode execution.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List
import re

MAX_TOKEN_SUPPLY = 10**18
MAX_TOKEN_AMOUNT = 10**18
OPCODE_TO_NAME = {
    0x01: "PUSH",
    0x02: "LOAD",
    0x03: "STORE",
    0x04: "ADD",
    0x05: "SUB",
    0x06: "MUL",
    0x07: "DIV",
    0x08: "EQ",
    0x09: "GT",
    0x0A: "LT",
    0x0B: "DUP",
    0x0C: "SWAP",
    0x0D: "JUMP",
    0x0E: "JUMPI",
    0x0F: "BLOCK_HEIGHT",
    0x10: "CREATE_CONTRACT",
    0x11: "CREATE_TOKEN",
    0x12: "MINT_TOKEN",
    0x13: "TRANSFER_TOKEN",
    0x14: "ASSERT",
    0x15: "EMIT",
    0xFF: "HALT",
}


class MVMError(Exception):
    pass


@dataclass
class MVMState:
    stack: List[int] = field(default_factory=list)
    memory: Dict[str, int] = field(default_factory=dict)
    events: List[Dict[str, Any]] = field(default_factory=list)
    contracts: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    tokens: Dict[str, Dict[str, Any]] = field(default_factory=dict)
    pc: int = 0
    halted: bool = False


class MVM:
    def __init__(self, program: List[Dict[str, Any]], context: Dict[str, int] | None = None) -> None:
        self.program = program
        self.state = MVMState(memory=dict(context or {}))

    def run(self) -> MVMState:
        while not self.state.halted and self.state.pc < len(self.program):
            instruction = self.program[self.state.pc]
            self._exec(instruction)
            self.state.pc += 1
        return self.state

    def _exec(self, ins: Dict[str, Any]) -> None:
        op = ins.get("op")
        if not op:
            raise MVMError(f"instruction at pc={self.state.pc} missing op")

        if op == "PUSH":
            self.state.stack.append(int(ins["value"]))
        elif op == "LOAD":
            key = ins["key"]
            self.state.stack.append(int(self.state.memory.get(key, 0)))
        elif op == "STORE":
            key = ins["key"]
            self.state.memory[key] = self._pop()
        elif op == "ADD":
            b, a = self._pop(), self._pop()
            self.state.stack.append(a + b)
        elif op == "SUB":
            b, a = self._pop(), self._pop()
            self.state.stack.append(a - b)
        elif op == "MUL":
            b, a = self._pop(), self._pop()
            self.state.stack.append(a * b)
        elif op == "DIV":
            b, a = self._pop(), self._pop()
            if b == 0:
                raise MVMError("division by zero")
            self.state.stack.append(a // b)
        elif op == "EQ":
            b, a = self._pop(), self._pop()
            self.state.stack.append(1 if a == b else 0)
        elif op == "GT":
            b, a = self._pop(), self._pop()
            self.state.stack.append(1 if a > b else 0)
        elif op == "LT":
            b, a = self._pop(), self._pop()
            self.state.stack.append(1 if a < b else 0)
        elif op == "ASSERT":
            cond = self._pop()
            if cond == 0:
                message = ins.get("message", "assertion failed")
                raise MVMError(message)
        elif op == "EMIT":
            name = ins.get("name", "event")
            value = self._pop() if ins.get("pop", True) else (self.state.stack[-1] if self.state.stack else 0)
            self.state.events.append({"name": name, "value": value})
        elif op == "DUP":
            if not self.state.stack:
                raise MVMError(f"stack underflow at pc={self.state.pc}")
            self.state.stack.append(int(self.state.stack[-1]))
        elif op == "SWAP":
            if len(self.state.stack) < 2:
                raise MVMError(f"stack underflow at pc={self.state.pc}")
            self.state.stack[-1], self.state.stack[-2] = self.state.stack[-2], self.state.stack[-1]
        elif op == "JUMP":
            target = int(ins["target"])
            self._check_pc(target)
            self.state.pc = target - 1
        elif op == "JUMPI":
            target = int(ins["target"])
            cond = self._pop()
            if cond != 0:
                self._check_pc(target)
                self.state.pc = target - 1
        elif op == "BLOCK_HEIGHT":
            self.state.stack.append(int(self.state.memory.get("block_height", 0)))
        elif op == "CREATE_CONTRACT":
            contract_id = str(ins["contract_id"])
            self.state.contracts[contract_id] = {
                "code_hash": str(ins.get("code_hash", "")),
                "creator": str(ins.get("creator", "unknown")),
                "created_at_height": int(self.state.memory.get("block_height", 0)),
            }
            self.state.events.append({"name": "ContractCreated", "contract_id": contract_id})
        elif op == "CREATE_TOKEN":
            contract_id = str(ins["contract_id"])
            symbol = str(ins["symbol"])
            decimals = int(ins.get("decimals", 18))
            total_supply = int(ins["supply"])
            owner = str(ins.get("owner", "owner"))
            self._require_address(owner)
            if decimals < 0 or decimals > 30:
                raise MVMError("token decimals must be in range [0,30]")
            if total_supply <= 0 or total_supply > MAX_TOKEN_SUPPLY:
                raise MVMError("token supply must be in range [1, 10^18]")
            if symbol in self.state.tokens:
                raise MVMError(f"token '{symbol}' already exists")
            self._require_symbol(symbol)
            self.state.tokens[symbol] = {
                "contract_id": contract_id,
                "name": str(ins.get("name", symbol)),
                "decimals": decimals,
                "total_supply": total_supply,
                "created_at_height": int(self.state.memory.get("block_height", 0)),
                "last_height": int(self.state.memory.get("block_height", 0)),
                "balances": {owner: total_supply},
            }
            self.state.events.append({"name": "TokenCreated", "symbol": symbol, "supply": total_supply})
        elif op == "MINT_TOKEN":
            symbol = str(ins["symbol"])
            to = str(ins["to"])
            amount = int(ins["amount"])
            if amount <= 0 or amount > MAX_TOKEN_AMOUNT:
                raise MVMError("mint amount must be in range [1, 10^18]")
            self._require_address(to)
            token = self.state.tokens.get(symbol)
            if token is None:
                raise MVMError(f"token '{symbol}' not found")
            self._check_token_height_sync(token)
            token["total_supply"] = int(token["total_supply"]) + amount
            balances = token["balances"]
            balances[to] = int(balances.get(to, 0)) + amount
            token["last_height"] = int(self.state.memory.get("block_height", 0))
            self.state.events.append({"name": "TokenMint", "symbol": symbol, "to": to, "amount": amount})
        elif op == "TRANSFER_TOKEN":
            symbol = str(ins["symbol"])
            from_addr = str(ins["from"])
            to_addr = str(ins["to"])
            amount = int(ins["amount"])
            if amount <= 0 or amount > MAX_TOKEN_AMOUNT:
                raise MVMError("transfer amount must be in range [1, 10^18]")
            self._require_address(from_addr)
            self._require_address(to_addr)
            if from_addr == to_addr:
                raise MVMError("sender and receiver token addresses must differ")
            token = self.state.tokens.get(symbol)
            if token is None:
                raise MVMError(f"token '{symbol}' not found")
            self._check_token_height_sync(token)
            balances = token["balances"]
            from_balance = int(balances.get(from_addr, 0))
            if from_balance < amount:
                raise MVMError("insufficient token balance")
            balances[from_addr] = from_balance - amount
            balances[to_addr] = int(balances.get(to_addr, 0)) + amount
            token["last_height"] = int(self.state.memory.get("block_height", 0))
            self.state.events.append({"name": "TokenTransfer", "symbol": symbol, "from": from_addr, "to": to_addr, "amount": amount})
        elif op == "HALT":
            self.state.halted = True
        else:
            raise MVMError(f"unknown opcode '{op}' at pc={self.state.pc}")

    def _pop(self) -> int:
        if not self.state.stack:
            raise MVMError(f"stack underflow at pc={self.state.pc}")
        return int(self.state.stack.pop())

    def _check_pc(self, pc: int) -> None:
        if pc < 0 or pc >= len(self.program):
            raise MVMError(f"jump target out of range: {pc}")

    def _require_address(self, address: str) -> None:
        if not re.fullmatch(r"[A-Za-z0-9_:\\-]{3,128}", address):
            raise MVMError(f"invalid token address '{address}'")

    def _require_symbol(self, symbol: str) -> None:
        if not re.fullmatch(r"[A-Z0-9]{2,16}", symbol):
            raise MVMError("token symbol must match [A-Z0-9]{2,16}")

    def _check_token_height_sync(self, token: Dict[str, Any]) -> None:
        current_height = int(self.state.memory.get("block_height", 0))
        last_height = int(token.get("last_height", 0))
        if current_height < last_height:
            raise MVMError("block height moved backwards for token state")


def load_json(path: Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def _read_str(buf: bytes, i: int) -> tuple[str, int]:
    if i >= len(buf):
        raise MVMError("bytecode truncated while reading string length")
    n = buf[i]
    i += 1
    if i + n > len(buf):
        raise MVMError("bytecode truncated while reading string payload")
    return buf[i : i + n].decode("utf-8"), i + n


def decode_bytecode_hex(bytecode_hex: str) -> List[Dict[str, Any]]:
    bytecode_hex = bytecode_hex.strip()
    if bytecode_hex.startswith(("0x", "0X")):
        bytecode_hex = bytecode_hex[2:]
    if len(bytecode_hex) % 2 != 0:
        raise MVMError("bytecode hex must have even length")
    buf = bytes.fromhex(bytecode_hex)
    i = 0
    program: List[Dict[str, Any]] = []
    while i < len(buf):
        opcode = buf[i]
        i += 1
        op = OPCODE_TO_NAME.get(opcode)
        if op is None:
            raise MVMError(f"unknown bytecode opcode 0x{opcode:02x}")
        ins: Dict[str, Any] = {"op": op}
        if op == "PUSH":
            if i + 8 > len(buf):
                raise MVMError("bytecode truncated for PUSH")
            ins["value"] = int.from_bytes(buf[i : i + 8], "little", signed=False)
            i += 8
        elif op in {"LOAD", "STORE"}:
            ins["key"], i = _read_str(buf, i)
        elif op in {"JUMP", "JUMPI"}:
            if i + 2 > len(buf):
                raise MVMError("bytecode truncated for jump")
            ins["target"] = int.from_bytes(buf[i : i + 2], "little", signed=False)
            i += 2
        elif op == "ASSERT":
            ins["message"], i = _read_str(buf, i)
        elif op == "EMIT":
            ins["name"], i = _read_str(buf, i)
            if i >= len(buf):
                raise MVMError("bytecode truncated for EMIT pop flag")
            ins["pop"] = buf[i] != 0
            i += 1
        elif op == "CREATE_CONTRACT":
            ins["contract_id"], i = _read_str(buf, i)
            ins["code_hash"], i = _read_str(buf, i)
            ins["creator"], i = _read_str(buf, i)
        elif op == "CREATE_TOKEN":
            ins["contract_id"], i = _read_str(buf, i)
            ins["symbol"], i = _read_str(buf, i)
            ins["name"], i = _read_str(buf, i)
            ins["owner"], i = _read_str(buf, i)
            if i + 9 > len(buf):
                raise MVMError("bytecode truncated for CREATE_TOKEN")
            ins["supply"] = int.from_bytes(buf[i : i + 8], "little", signed=False)
            i += 8
            ins["decimals"] = int(buf[i])
            i += 1
        elif op == "MINT_TOKEN":
            ins["symbol"], i = _read_str(buf, i)
            ins["to"], i = _read_str(buf, i)
            if i + 8 > len(buf):
                raise MVMError("bytecode truncated for MINT_TOKEN")
            ins["amount"] = int.from_bytes(buf[i : i + 8], "little", signed=False)
            i += 8
        elif op == "TRANSFER_TOKEN":
            ins["symbol"], i = _read_str(buf, i)
            ins["from"], i = _read_str(buf, i)
            ins["to"], i = _read_str(buf, i)
            if i + 8 > len(buf):
                raise MVMError("bytecode truncated for TRANSFER_TOKEN")
            ins["amount"] = int.from_bytes(buf[i : i + 8], "little", signed=False)
            i += 8
        program.append(ins)
    return program


def main() -> int:
    parser = argparse.ArgumentParser(description="Execute MVM bytecode JSON")
    parser.add_argument("--program", default="", help="Path to MVM program JSON")
    parser.add_argument("--bytecode", default="", help="Path to bytecode hex file produced by mvmlc")
    parser.add_argument("--bytecode-hex", default="", help="Raw bytecode hex string")
    parser.add_argument("--context", default="", help="Optional context JSON file")
    args = parser.parse_args()

    if not any([args.program, args.bytecode, args.bytecode_hex]):
        print("error: one of --program, --bytecode, or --bytecode-hex is required", file=sys.stderr)
        return 1

    program_doc: Dict[str, Any] = {"mvm": "1.0"}
    if args.program:
        program_doc = load_json(Path(args.program))
        program = program_doc.get("program")
        if not isinstance(program, list):
            print("error: 'program' must be a list", file=sys.stderr)
            return 1
    else:
        bytecode_hex = args.bytecode_hex
        if args.bytecode:
            bytecode_hex = Path(args.bytecode).read_text(encoding="utf-8").strip()
        try:
            program = decode_bytecode_hex(bytecode_hex)
        except MVMError as err:
            print(f"error: {err}", file=sys.stderr)
            return 1
        program_doc["mvm"] = "1.1-bytecode"

    context: Dict[str, int] = {}
    if args.context:
        context_doc = load_json(Path(args.context))
        context = {k: int(v) for k, v in context_doc.items()}

    vm = MVM(program=program, context=context)
    try:
        state = vm.run()
    except MVMError as err:
        print(f"execution_error: {err}", file=sys.stderr)
        return 2

    result = {
        "mvm": program_doc.get("mvm", "1.0"),
        "stack": state.stack,
        "memory": state.memory,
        "events": state.events,
        "contracts": state.contracts,
        "tokens": state.tokens,
        "halted": state.halted,
        "pc": state.pc,
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
