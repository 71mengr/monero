# MVM - Monero Virtual Machine

MVM is a **Monero-native VM-style execution layer** for this codebase.

It provides deterministic bytecode-like execution for contract logic that is
intended to control Monero workflows (multisig, timelocks, oracle gating),
not EVM-compatible on-chain execution.

## Why MVM

- You asked for "Monero's own EVM"; this introduces **MVM** as a dedicated VM model.
- Contracts are represented as JSON bytecode and executed deterministically.
- Output state/events can drive transaction coordinators for Monero actions.

## Files

- `mvm_runtime.py` - MVM interpreter CLI.
- `examples/escrow_unlock.mvm.json` - example program.
- `examples/escrow_unlock.context.json` - execution context.

## Opcodes (v1.1)

- Stack/memory: `PUSH`, `LOAD`, `STORE`
- Arithmetic: `ADD`, `SUB`, `MUL`, `DIV`
- Comparison: `EQ`, `GT`, `LT`
- Stack/control: `DUP`, `SWAP`, `JUMP`, `JUMPI`
- Chain context: `BLOCK_HEIGHT`
- Contract/token: `CREATE_CONTRACT`, `CREATE_TOKEN`, `MINT_TOKEN`, `TRANSFER_TOKEN`
- Control/assertion: `ASSERT`, `HALT`
- Eventing: `EMIT`

Security checks in runtime:
- Token symbol policy (`[A-Z0-9]{2,16}`)
- Token address policy (`[A-Za-z0-9_:\\-]{3,128}`)
- Total supply and transfer/mint amount bounds (`1..10^18`)
- Sender and receiver token addresses must differ
- Per-token block-height monotonicity (`last_height`) for sync safety

## Run example

```bash
python3 contrib/mvm/mvm_runtime.py \
  --program contrib/mvm/examples/escrow_unlock.mvm.json \
  --context contrib/mvm/examples/escrow_unlock.context.json
```

Expected behavior:

- asserts unlock conditions,
- stores `release_amount_atomic`,
- emits `ReleaseApproved`,
- halts successfully.

## Integration with MSC

You can use MVM output (`memory`, `events`) as the deterministic decision engine
for `contrib/monero_contracts` packages.

## Token example (USDT-like)

```bash
python3 contrib/mvm/mvm_runtime.py \
  --program contrib/mvm/examples/usdt_token.mvm.json \
  --context contrib/mvm/examples/usdt_token.context.json
```

This example demonstrates `PUSH` + `ADD`, then creates a contract and token,
records token state, emits execution events, and captures `block_height`.

## MVC source -> bytecode (custom type, not Solidity)

Use `mvmlc.py` to compile `.mvc` source into bytecode hex:

```bash
python3 contrib/mvm/mvmlc.py \
  --input contrib/mvm/examples/usdt_token.mvc \
  --output /tmp/usdt_token.bytecode
```

Then create contract directly from bytecode in wallet:

```bash
mvm_create_contract /tmp/usdt_token.bytecode
```

or

```bash
mvm_create_contract <bytecode_hex> <address> <amount>
```

Run bytecode directly in runtime:

```bash
python3 contrib/mvm/mvm_runtime.py --bytecode /tmp/usdt_token.bytecode --context contrib/mvm/examples/usdt_token.context.json
```

## Front-end dApp wallet connector

A minimal wallet-connect dApp is available at:

- `contrib/mvm/dapp/index.html`

It supports:
- EVM wallet connect (`window.ethereum`)
- Monero provider connect (`window.monero`, when wallet extension provides it)
- `/get_web3_network` fetch
- local MVM payload draft building from bytecode

## Mainnet integration notes

- Core indexing path only applies MVM contract metadata on `MAINNET`.
- Wallet CLI MVM commands are gated to mainnet-enabled networks.
