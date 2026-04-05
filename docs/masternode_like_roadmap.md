# Masternode-like Architecture Roadmap for Monero

## Important note

Dash-style masternodes cannot be added safely as a small patch, because they require
consensus, networking, wallet, miner, and economic policy changes that would need a
coordinated hard fork.

This document adds a concrete implementation plan to move from idea to production in
stages, with explicit safety gates.

## Target capability

Introduce an optional **bonded validator tier** ("masternode-like" nodes) with a
primary security objective of increasing the cost and operational complexity of
coordinated 51% attacks and large-scale chain rewrite attempts.

The tier is designed to complement PoW, not replace it, by adding deterministic
validator-set checks and economic penalties around critical network duties.

Concretely, bonded validators should:

- Post a locked collateral output that can be delayed/slashed for protocol faults.
- Provide measurable network services (high-availability relay, block propagation,
  optional routing services).
- Participate in deterministic, chain-derived security workflows that make deep
  transaction-history rewrites substantially harder to execute unnoticed.
- Communicate with each other through authenticated P2P protocol messages (not
  ad-hoc RPC polling) for validator coordination duties.
- Receive deterministic protocol rewards when they meet performance criteria.
- Be penalized and/or de-registered for non-performance or provable misconduct.

## Minimum protocol changes

1. **Collateral registration transaction type**
   - New tx payload that locks collateral for a minimum term.
   - On-chain metadata commitment for operator key and service endpoints.

2. **Deterministic active set selection**
   - Use chain-derived randomness and eligibility rules to produce an active set per
     epoch.

3. **Reward split update**
   - Update coinbase reward logic to include bonded validator payouts.
   - Keep miner + tail emission invariants explicitly documented.

4. **Penalty and unlock rules**
   - Missed duties threshold.
   - Deregistration proof format.
   - Unlock delay and slashing (if enabled).

5. **P2P service proofs and inter-validator messaging**
   - Signed uptime heartbeats.
   - Optional challenge/response proofs for relay quality.
   - Explicit P2P command set for validator-to-validator coordination and
     authenticated gossip of duty results.

## Non-consensus components

- New daemon subsystem for bonded validator state cache.
- Operator CLI flows:
  - register
  - update endpoint
  - resign
  - unlock collateral after delay
- RPC additions:
  - `get_bonded_validators`
  - `get_bonded_validator_status`
  - `get_bonded_validator_rewards`

## Suggested phased delivery

### Phase 0: Research + threat model
- Formalize Sybil, liveness, and censorship risks.
- Quantify required collateral economics.

### Phase 1: Testnet-only feature gate
- Add feature flag disabled on mainnet.
- Implement registration, active set, read-only RPC.

### Phase 2: Incentives on testnet
- Enable deterministic payouts.
- Run adversarial and long-horizon simulations.

### Phase 3: Audit + hard-fork proposal
- Independent cryptography + consensus audits.
- Publish hard-fork spec with migration plan.

## Security checklist

- Consensus encoding is canonical and deterministic.
- Replay and equivocation protections on all validator proofs.
- Wallet UX prevents accidental collateral lock misuse.
- Full regression coverage for reorg behavior.

## Safety checklist (must-have tests)

- Encoding round-trip and malleability tests.
- Block validation tests (accept/reject vectors).
- Reorg tests (state rollback correctness).
- Duplicate/regression tests for uniqueness constraints.
- Reward accounting invariants.

The roadmap already emphasizes parser/serializer tests and state-machine tests; those become mandatory before activation.

## Compatibility constraints

- All changes must be hidden behind a network upgrade version.
- Nodes that do not upgrade must fail safely at fork height.
- Reward and supply accounting must match expected emission curves.

## Next implementation steps in code

1. Add an RFC spec in `docs/rfc/` with binary serialization rules.
2. Add new tx extra parser + serializer tests.
3. Add core state machine tests for registration/expiry/penalty.
4. Add testnet-only RPC endpoints and integration tests.

