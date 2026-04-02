# RFC: Bonded Validator Tier (Testnet-first)

## Status

- **Date:** April 1, 2026
- **State:** Draft (API scaffolding merged, consensus rules disabled)

## Objective

Define a deterministic bonded validator subsystem where operators lock collateral,
serve signed service duties, and receive protocol rewards with explicit penalties
for missed obligations.

## Security checklist

- [ ] Consensus encoding is canonical and deterministic.
- [ ] Replay and equivocation protections on all validator proofs.
- [ ] Wallet UX prevents accidental collateral lock misuse.
- [ ] Full regression coverage for reorg behavior.

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

This section defines consensus-only, deterministic, and reorg-safe handling of
missed duties, deregistration, proof validation, and collateral release.

### i) Missed-duty scoring

Each validator `v` has an on-chain duty score over a fixed sliding window:

- `duty_window_epochs` (consensus constant; example: 8 epochs).
- Per epoch `E`, derive deterministic duty opportunities:
  - `assigned_duties(v, E)`: integer count produced from canonical assignment
    rules.
  - `missed_duties(v, E)`: count of duties lacking a valid proof by
    `proof_deadline_blocks` after assignment.
- Compute windowed score at epoch `E`:
  - `A(v, E) = sum(assigned_duties(v, e))` for `e in [E - W + 1, E]`
  - `M(v, E) = sum(missed_duties(v, e))` for same range
  - `miss_ratio_bps(v, E) = floor(10000 * M(v, E) / max(1, A(v, E)))`

`assigned_duties` and `missed_duties` MUST be derivable from canonical chain
state only. Mempool presence, peer-local observations, and wall-clock time MUST
NOT affect scoring.

### ii) Thresholds for warnings, penalties, and deregistration

Use basis-point thresholds fixed by network version:

- `warn_threshold_bps`
- `penalty_threshold_bps`
- `deregister_threshold_bps`
- with required ordering:
  `0 <= warn_threshold_bps <= penalty_threshold_bps <= deregister_threshold_bps <= 10000`

At each epoch boundary:

1. If `miss_ratio_bps < warn_threshold_bps`: no action.
2. If `warn_threshold_bps <= miss_ratio_bps < penalty_threshold_bps`:
   non-removal warning state (consensus-visible flag only).
3. If `penalty_threshold_bps <= miss_ratio_bps < deregister_threshold_bps`:
   reward haircut using deterministic function
   `haircut_bps = f(miss_ratio_bps)` (where `f` is consensus-defined and
   monotonic).
4. If `miss_ratio_bps >= deregister_threshold_bps`: validator enters
   `deregistered_pending_finality`.

To avoid reorg thrash, final state transition to `deregistered` occurs only
after `dereg_finality_depth` blocks have confirmed the triggering evidence.

### iii) Deregistration proof format (quorum attestations)

Define canonical on-chain object `deregistration_proof_v1`:

- `version` (u8)
- `validator_id` (fixed-size canonical encoding)
- `epoch_index` and `duty_slot` (or challenge tuple) identifying missed duty
- `reason_code` (enum; e.g. missed-heartbeat, missed-challenge, equivocation)
- `evidence_root` (hash of canonical evidence payload)
- `quorum_id` (deterministically derived from epoch randomness)
- `attestation_bitmap` (bitset of quorum members who signed)
- `quorum_signature` (aggregated signature) OR ordered individual signatures
  with canonical index order

Validation rules:

1. Recompute quorum membership deterministically from chain state at the duty
   epoch.
2. Verify signer set cardinality `>= quorum_threshold`.
3. Verify all signatures over exact domain-separated message:
   `H("bonded-dereg-proof-v1" || core_fields)`.
4. Enforce uniqueness with key
   `(validator_id, epoch_index, duty_slot, reason_code)` to prevent duplicate
   penalties.
5. Reject non-canonical encodings (field order, length, signature ordering).

Proof acceptance MUST be purely a function of block contents plus referenced
historical chain state, making it deterministic across nodes and replay-safe.

### iv) Slashing and unlock-delay semantics

On finalized deregistration (or voluntary resign), collateral transitions by a
deterministic state machine:

1. `active` -> `exiting_locked` at `event_height`.
2. Optional slash at `event_height`:
   - `slash_bps = slash_table[reason_code][severity_band]`
   - `slashed_amount = floor(collateral_locked * slash_bps / 10000)`
   - burn or route destination is consensus-defined.
3. Remaining collateral enters timelock until:
   - `unlock_height = event_height + unlock_delay_blocks`.
4. Claims are valid only at/after `unlock_height` and only for unslashed
   remainder.

Additional semantics:

- Slashing MUST be one-shot per unique offense key.
- If multiple offenses are included in one block, apply canonical ordering by
  `(event_height, reason_code, evidence_hash)` before computing cumulative slash.
- Total slash MUST be capped at `collateral_locked` (saturating arithmetic).
- Reorg handling is automatic: if trigger blocks are detached before
  `dereg_finality_depth`, state rolls back and no lasting slash/unlock transition
  remains.

---

## 6) Reward eligibility and payout rules

Consensus must define reward eligibility and payout behavior with exact formulas
that are reproducible from canonical chain state.

### i) Activation delay after registration

A newly registered validator becomes reward-eligible at:

- `eligible_height = registration_height + reward_activation_delay_blocks`

Where `reward_activation_delay_blocks` is a consensus parameter fixed by the
active network version. Before `eligible_height`, the validator MAY exist in
state but MUST NOT receive any bonded reward output.

### ii) Deterministic active set per epoch

For epoch `E`, the active set MUST be derived using only finalized chain data
and deterministic randomness:

1. Build `eligible_validators(E)` from on-chain validator records that satisfy:
   - `eligible_height <= epoch_start_height(E)`
   - collateral still locked for the required horizon
   - not deregistered/slashed/in cooldown at `epoch_start_height(E)`
2. Derive `epoch_randomness(E)` from a chain anchor hash at the epoch boundary
   with domain separation (for example:
   `H("bonded-validator-epoch-v1" || boundary_block_hash || E)`).
3. Sort/shuffle deterministically using `(epoch_randomness(E), validator_id)`
   and select the first `active_set_size` entries.

No local wall-clock, peer-observed liveness cache, or mempool-only data may
influence active-set membership.

### iii) Deterministic coinbase reward share

Miner transaction validation MUST enforce a deterministic reward split that is
computable by every verifier from block and chain context:

- `validator_share = floor(base_block_reward * validator_basis_points / 10000)`
- `miner_share = base_block_reward - validator_share`
- fees/tail-emission handling follows consensus-defined invariants

If per-validator distribution is enabled, each recipient amount MUST be derived
from `active_set(E)` using canonical ordering and deterministic residual
assignment (for example, distribute remainder to lowest hash-ranked validators).

The roadmap requirement is that both active-set selection and bonded reward
allocation are pure functions of chain state plus deterministic randomness.

---

## 7) P2P service proofs

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

## 8) Non-consensus components

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

## Mandatory pre-activation test gate

Before any activation step in Phase B or later, the implementation MUST include
and pass the following minimum test suite:

- Encoding round-trip and malleability tests.
- Block validation tests (accept/reject vectors).
- Reorg tests (state rollback correctness).
- Duplicate/regression tests for uniqueness constraints.
- Reward accounting invariants.

The roadmap already emphasizes parser/serializer tests and state-machine tests;
those are mandatory and MUST pass before activation.

## Open questions

- Final collateral bounds and denomination granularity.
- Exact duty definitions and evidence model.
- Slashing activation at first fork vs delayed fork.
- Impact of endpoint metadata privacy vs transparency.
