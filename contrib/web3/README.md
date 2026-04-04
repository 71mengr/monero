# Monero Web3 Smart Contract Kit

This folder adds an **Ethereum-compatible smart contract** that is purpose-built for the Monero daemon Web3 RPC additions in this repo.

## Contract

- `contracts/MoneroNetworkRegistry.sol`

The contract stores and indexes records emitted/served by Monero daemon Web3 endpoints (`/get_web3_network`, `eth_chainId`, `monero_web3_network`):

- `network` (`mainnet`, `testnet`, `stagenet`, `fakechain`)
- `chainIdHex`
- `networkId`
- `web3UniqueId`
- daemon metadata (`daemonEndpoint`, `daemonVersion`)

## What you get

- Owner + writer role controls
- Pause/unpause support
- Deterministic profile key (`keccak256(network|networkId|web3UniqueId)`)
- Create/upsert/remove profiles
- Validation for Monero nettypes and `0x` + 40-hex `web3UniqueId`
- Events for indexing in off-chain infra

## Minimal usage flow

1. Query your Monero daemon:

```bash
curl -s http://127.0.0.1:18081/get_web3_network
```

2. Deploy `MoneroNetworkRegistry` on your EVM chain.

3. Write daemon identity into the contract with `createProfile` or `upsertProfile`.

4. Consumers resolve a record via `profileKey(...)` + `getProfile(...)`.

## Example with Foundry cast

See `scripts/register_profile_cast.sh` for a ready-to-run registration script.

## Security notes

- Restrict writer keys; only trusted operators should publish profiles.
- Treat this contract as metadata anchoring, not a bridge for Monero funds.
- Keep contract paused during incident response and rotate writer keys.
