#!/usr/bin/env python3
"""
Monero Smart Contract (MSC) Builder

Creates deterministic, Monero-native contract packages for operational execution.
This is NOT an on-chain VM; it is a contract artifact format for Monero workflows
(multisig, time-lock, and oracle-driven settlement) with deterministic IDs.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import sys
import urllib.request
from pathlib import Path
from typing import Any, Dict

ALLOWED_CONTRACT_TYPES = {"escrow", "milestone", "subscription", "bounty"}
ALLOWED_ACTIONS = {
    "release_multisig",
    "refund_multisig",
    "split_multisig",
    "rotate_signer",
    "freeze_until_height",
}


def _canonical_json(data: Any) -> str:
    return json.dumps(data, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _sha3_hex(payload: str) -> str:
    return hashlib.sha3_256(payload.encode("utf-8")).hexdigest()


def _fetch_web3_network(rpc_url: str) -> Dict[str, Any]:
    with urllib.request.urlopen(f"{rpc_url.rstrip('/')}/get_web3_network", timeout=5) as response:
        if response.status != 200:
            raise RuntimeError(f"RPC request failed with HTTP {response.status}")
        parsed = json.loads(response.read().decode("utf-8"))

    required = ["network", "network_id", "web3_unique_id", "chain_id_hex"]
    missing = [key for key in required if key not in parsed]
    if missing:
        raise RuntimeError(f"RPC response missing fields: {', '.join(missing)}")
    return parsed


def _validate_contract(source: Dict[str, Any]) -> None:
    required = ["name", "contract_type", "network", "parties", "actions", "rules"]
    missing = [k for k in required if k not in source]
    if missing:
        raise ValueError(f"Missing required keys: {', '.join(missing)}")

    if source["contract_type"] not in ALLOWED_CONTRACT_TYPES:
        raise ValueError(
            f"Unsupported contract_type '{source['contract_type']}'. "
            f"Allowed: {', '.join(sorted(ALLOWED_CONTRACT_TYPES))}"
        )

    if not isinstance(source["parties"], list) or len(source["parties"]) < 2:
        raise ValueError("'parties' must be a list with at least 2 participants")
    aliases = [str(p.get("alias", "")).strip() for p in source["parties"] if isinstance(p, dict)]
    if len(aliases) != len(source["parties"]) or any(not alias for alias in aliases):
        raise ValueError("each party must be an object with a non-empty 'alias'")
    if len(set(aliases)) != len(aliases):
        raise ValueError("party aliases must be unique")

    if not isinstance(source["actions"], list) or len(source["actions"]) == 0:
        raise ValueError("'actions' must be a non-empty list")

    for idx, action in enumerate(source["actions"]):
        if not isinstance(action, dict):
            raise ValueError(f"actions[{idx}] must be an object")
        if "type" not in action:
            raise ValueError(f"actions[{idx}] missing 'type'")
        if action["type"] not in ALLOWED_ACTIONS:
            raise ValueError(
                f"actions[{idx}].type '{action['type']}' unsupported. "
                f"Allowed: {', '.join(sorted(ALLOWED_ACTIONS))}"
            )


def _build_hashable_package(package: Dict[str, Any]) -> Dict[str, Any]:
    # Keep package hashes reproducible: operational timestamps are metadata and
    # intentionally excluded from the canonical package digest.
    return {
        "schema": package["schema"],
        "contract_id": package["contract_id"],
        "contract": package["contract"],
        "terms_hash": package["terms_hash"],
        "network_binding": package["network_binding"],
        "execution_notes": package["execution_notes"],
    }


def build_contract_package(
    source: Dict[str, Any], rpc_meta: Dict[str, Any] | None, created_at: str | None = None
) -> Dict[str, Any]:
    _validate_contract(source)

    now = created_at or dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat()
    source_canonical = _canonical_json(source)
    terms_hash = _sha3_hex(source_canonical)

    identity_seed = {
        "network": source["network"],
        "contract_type": source["contract_type"],
        "terms_hash": terms_hash,
        "parties": sorted(p.get("alias", "") for p in source["parties"]),
    }
    if rpc_meta is not None:
        identity_seed["network_id"] = rpc_meta["network_id"]
        identity_seed["web3_unique_id"] = rpc_meta["web3_unique_id"]
        identity_seed["chain_id_hex"] = rpc_meta["chain_id_hex"]

    contract_id = f"msc_{_sha3_hex(_canonical_json(identity_seed))[:40]}"

    package = {
        "schema": "msc/1.0",
        "contract_id": contract_id,
        "created_at": now,
        "contract": source,
        "terms_hash": terms_hash,
        "network_binding": rpc_meta,
        "execution_notes": {
            "engine": "off-chain-monero-native",
            "description": (
                "Execute actions through Monero primitives (multisig, timelock, signer rotation) "
                "when all rule conditions are met."
            ),
        },
    }

    package["package_hash"] = _sha3_hex(_canonical_json(_build_hashable_package(package)))
    return package


def main() -> int:
    parser = argparse.ArgumentParser(description="Build deterministic Monero Smart Contract package")
    parser.add_argument("--input", required=True, help="Path to source contract JSON")
    parser.add_argument("--output", required=True, help="Path to output package JSON")
    parser.add_argument("--rpc-url", default="", help="Optional monerod RPC URL for network binding")
    parser.add_argument(
        "--created-at",
        default="",
        help="Optional RFC3339 timestamp override used for metadata only",
    )
    args = parser.parse_args()

    src_path = Path(args.input)
    out_path = Path(args.output)

    if not src_path.exists():
        print(f"error: input not found: {src_path}", file=sys.stderr)
        return 1

    try:
        source = json.loads(src_path.read_text(encoding="utf-8"))
        rpc_meta = _fetch_web3_network(args.rpc_url) if args.rpc_url else None
        created_at = args.created_at.strip() or None
        package = build_contract_package(source, rpc_meta, created_at=created_at)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(json.dumps(package, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    except Exception as exc:  # noqa: BLE001 - CLI tool surfaces all errors
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"built: {out_path}")
    print(f"contract_id: {package['contract_id']}")
    print(f"package_hash: {package['package_hash']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
