# Monero Smart Contracts (MSC) - Monero-native contract artifacts

This module is a **Monero-native smart contract framework** for this codebase.

> Monero does not execute Ethereum-style on-chain bytecode.
> MSC provides a deterministic contract package format that maps to Monero primitives
> (multisig, signer policies, timelocks, and oracle conditions).

## What makes this unique to this codebase

- Binds contract packages to this repo's daemon Web3 identity endpoint (`/get_web3_network`) when `--rpc-url` is provided.
- Uses the Monero-specific identity fields (`network_id`, `web3_unique_id`, `chain_id_hex`) as optional network binding.
- Produces deterministic IDs (`msc_<40 hex>`) and package hashes for reproducible audit trails.

## Files

- `msc_builder.py` - CLI builder that validates source contracts and emits deterministic MSC packages.
- `examples/escrow_contract.json` - Example source contract for escrow workflow.

## Build a contract package

Without daemon binding:

```bash
python3 contrib/monero_contracts/msc_builder.py \
  --input contrib/monero_contracts/examples/escrow_contract.json \
  --output /tmp/msc_escrow_package.json
```

With daemon binding to current node identity:

```bash
python3 contrib/monero_contracts/msc_builder.py \
  --input contrib/monero_contracts/examples/escrow_contract.json \
  --output /tmp/msc_escrow_package.json \
  --rpc-url http://127.0.0.1:18081
```

## Output fields

The generated package includes:

- `schema` (`msc/1.0`)
- `contract_id` (deterministic `msc_...`)
- `contract` (your original contract)
- `terms_hash` (canonical hash of source terms)
- `network_binding` (optional daemon Web3 metadata)
- `package_hash` (canonical hash of full package)

## Contract source requirements

The input JSON must include:

- `name`
- `contract_type` (`escrow`, `milestone`, `subscription`, `bounty`)
- `network`
- `parties` (2+)
- `rules`
- `actions` (1+)

Supported action types:

- `release_multisig`
- `refund_multisig`
- `split_multisig`
- `rotate_signer`
- `freeze_until_height`

## Operational model

1. Create & sign MSC package.
2. Share package hash with counterparties.
3. Watch conditions (`rules`) off-chain.
4. Execute listed actions using Monero transaction primitives.
5. Archive txids alongside `contract_id` for auditability.
