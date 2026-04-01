# RFC: Optional Bonded Validator Tier (Testnet-first)

## Status

- **Date:** April 1, 2026
- **State:** Draft (scaffolding merged, consensus rules not active)

## Goal

Add an optional bonded validator tier that can be enabled at a future network upgrade.
Bonded validators post locked collateral, provide measurable service duties, earn deterministic rewards, and can be penalized/de-registered for non-performance.

## Scope in this patch

This change introduces **RPC surface scaffolding** so ecosystem tooling can integrate against stable endpoint shapes before consensus activation:

- `get_bonded_validators`
- `get_bonded_validator_status`
- `get_bonded_validator_rewards`

These endpoints currently return `enabled=false` and a deterministic reason string while the feature is disabled.

## Consensus behavior (future patch set)

A future hard-fork-gated patch set should implement:

1. Collateral registration tx semantics.
2. Deterministic active set selection per epoch.
3. Coinbase/reward split integration.
4. Missed-duty accounting and penalties.
5. De-registration and collateral unlock/slash rules.

## Rationale for phased approach

This follows a safer migration path:

1. Introduce API/read-only observability first.
2. Activate feature on testnet under explicit gate.
3. Run long-horizon simulations and adversarial testing.
4. Only then propose mainnet activation.
