# RFC: Bonded Validator Tier (Testnet-first)

## Status

- **Date:** April 1, 2026
- **State:** Draft (API scaffolding merged, consensus rules disabled)

## Objective

Define a deterministic bonded validator subsystem where operators lock collateral,
serve signed service duties, and receive protocol rewards with explicit penalties
for missed obligations.

## Design summary

This RFC decomposes the rollout into consensus and non-consensus components so
implementation can be staged safely:

1. Add transaction and state transition rules for bonded participation.
2. Select the active validator set deterministically per epoch.
3. Update coinbase reward accounting to include bonded payouts.
4. Add penalties, de-registration, and delayed collateral release.
5. Introduce daemon cache + operator RPC/CLI for observability and operations.

---

## 1) Collateral registration transaction type

Introduce a new transaction payload `tx_extra_bonded_registration` (name tentative)
with consensus-validated fields:

- `operator_pubkey`: long-term operator key used to sign service proofs.
- `collateral_amount`: must be within `[min_collateral, max_collateral]`.
- `lock_term_epochs`: minimum lock duration; may be extended but not reduced.
- `service_commitment`: hash commitment to service endpoint metadata.
- `registration_nonce`: replay protection and deterministic ordering tie-breaker.

### Consensus checks

- Collateral output(s) must be provably locked until `unlock_height` derived from
  registration height + minimum term + protocol delay.
- Registration is valid only if operator key is not already active or pending.
- Endpoint metadata itself is not consensus-critical; only the commitment hash is.
- Re-registration while currently bonded is rejected unless explicitly supported
  by an upgrade rule.

---

## 2) On-chain metadata commitment

Store only a commitment on-chain to avoid bloat and endpoint churn:

- Commitment preimage should include:
  - operator key
  - endpoint list (e.g. host, port, transport hints)
  - optional capabilities bitset
  - version/format tag
- Commitment algorithm: domain-separated hash with canonical serialization.

Recommended commitment domain:
`H("bonded-validator-metadata-v1" || canonical_metadata_blob)`

Endpoint updates use a dedicated update transaction carrying a new commitment,
subject to rate-limits per epoch to prevent update spam.

---

## 3) Deterministic active set selection

Per epoch, derive an active set from eligible bonded validators using chain data
only (no local clocks or mempool dependence).

### Eligibility

A validator is eligible when:

- collateral is locked and not in unlock queue,
- not deregistered/slashed within cooldown,
- has satisfied minimum proof freshness threshold.

### Randomness and selection

- Randomness seed: hash-derived from finalized chain data at epoch boundary
  (e.g. previous epoch checkpoint hash + domain separator).
- Selection: stable deterministic shuffle over eligible validator IDs.
- Active set size: protocol parameter, bounded by minimum and maximum limits.
- Tie-breaking: lexicographic order of validator ID.

All nodes must reproduce the same active set given identical chain state.

---

## 4) Reward split update

Update miner transaction construction and validation rules to include bonded
validator payouts.

### Invariants

- Preserve existing emission schedule and tail emission behavior.
- Preserve explicit miner reward component.
- Enforce:
  - `base_reward = miner_share + bonded_share (+ governance_share if enabled)`
  - sum of outputs equals consensus-calculated reward total.

### Payout policy (draft)

- Epoch reward pool allocated proportionally across active validators.
- Optional performance multiplier bounded in `[0, 1]`.
- Rounding residual handled deterministically (e.g. highest-ranked recipients).

---

## 5) Penalty and unlock rules

### Missed duties threshold

Track missed duty score over a sliding epoch window:

- Below warning threshold: no penalty.
- Between warning and penalty threshold: reward haircut.
- Above deregistration threshold: immediate removal from active set and start
  unlock delay.

### Deregistration proof format

A deregistration event should be triggerable by a quorum proof containing:

- accused validator ID,
- duty context (height/epoch/challenge ID),
- attestation signatures from required quorum,
- canonical reason code.

Proof format must be canonical and signature-verifiable on-chain.

### Unlock delay and slashing

- `unlock_delay_epochs` applies after voluntary resign or forced deregistration.
- If slashing is enabled at activation:
  - slash amount determined by reason code and offense severity,
  - unslashed remainder becomes claimable only after unlock delay.

---

## 6) P2P service proofs

### Signed uptime heartbeats

Validators periodically publish signed heartbeats:

- includes validator ID, timestamp/height anchor, and service digest,
- verified/gossiped by peers,
- indexed for duty accounting.

### Optional challenge/response proofs

For relay-quality verification, peers may issue deterministic challenges:

- challenge references recent chain anchor,
- validator response signed and time-bounded,
- failure contributes to missed duty score.

Challenge/response remains optional initially and can be activated later.

---

## 7) Non-consensus components

### Daemon subsystem

Add a bonded validator state cache for fast RPC and miner-template integration:

- epoch-indexed active set cache,
- validator lifecycle state machine cache,
- rewards accrual snapshot cache,
- persistence across daemon restart.

Cache must be derivable from chain state and safely rebuildable.

### Operator CLI flows

Add user-facing flows:

- `register`
- `update-endpoint`
- `resign`
- `unlock-collateral` (after delay)

CLI commands should support dry-run validation and explicit fee estimation.

### RPC additions

Expose stable endpoints:

- `get_bonded_validators`
- `get_bonded_validator_status`
- `get_bonded_validator_rewards`

While disabled by consensus gate, endpoints should return deterministic
`enabled=false` responses and feature-state metadata.

---

## Rollout plan

1. **Phase A (current):** RPC and data model scaffolding.
2. **Phase B:** Testnet activation behind HF gate with conservative params.
3. **Phase C:** Long soak, adversarial testing, and economics review.
4. **Phase D:** Mainnet proposal with explicit activation criteria.

## Open questions

- Final collateral bounds and denomination granularity.
- Exact duty definitions and evidence model.
- Slashing activation at first fork vs delayed fork.
- Impact of endpoint metadata privacy vs transparency.
