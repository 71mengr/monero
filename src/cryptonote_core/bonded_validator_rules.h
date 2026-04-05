#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "crypto/hash.h"
#include "cryptonote_config.h"
#include "serialization/keyvalue_serialization.h"
#include "serialization/serialization.h"

namespace cryptonote
{
  struct collateral_registration_tx_payload
  {
    uint64_t collateral_amount = 0;
    uint64_t lock_start_height = 0;
    uint64_t min_lock_blocks = 0;
    std::string operator_key;
    std::vector<std::string> service_endpoints;
    crypto::hash metadata_commitment = crypto::null_hash;

    bool is_valid(std::string* reason = nullptr) const;
  };

  struct bonded_validator_info
  {
    std::string id;
    std::string operator_key;
    std::string collateral_txid;
    uint64_t collateral_amount = 0;
    uint64_t registration_height = 0;
    uint64_t lock_end_height = 0;
    uint64_t last_uptime_proof_height = 0;
    uint32_t missed_duties = 0;
    uint32_t penalty_points = 0;
    bool active = false;
    bool online = false;
    bool deregistered = false;
    uint64_t created_height = 0;
    uint64_t updated_height = 0;
    uint64_t created_timestamp = 0;
    uint64_t updated_timestamp = 0;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(id)
      FIELD(operator_key)
      FIELD(collateral_txid)
      FIELD(collateral_amount)
      FIELD(registration_height)
      FIELD(lock_end_height)
      FIELD(last_uptime_proof_height)
      FIELD(missed_duties)
      FIELD(penalty_points)
      FIELD(active)
      FIELD(online)
      FIELD(deregistered)
      FIELD(created_height)
      FIELD(updated_height)
      FIELD(created_timestamp)
      FIELD(updated_timestamp)
    END_SERIALIZE()

    BEGIN_KV_SERIALIZE_MAP()
      KV_SERIALIZE(id)
      KV_SERIALIZE(operator_key)
      KV_SERIALIZE(collateral_txid)
      KV_SERIALIZE(collateral_amount)
      KV_SERIALIZE(registration_height)
      KV_SERIALIZE(lock_end_height)
      KV_SERIALIZE(last_uptime_proof_height)
      KV_SERIALIZE(missed_duties)
      KV_SERIALIZE(penalty_points)
      KV_SERIALIZE(active)
      KV_SERIALIZE(online)
      KV_SERIALIZE(deregistered)
      KV_SERIALIZE_OPT(created_height, (uint64_t)0)
      KV_SERIALIZE_OPT(updated_height, (uint64_t)0)
      KV_SERIALIZE_OPT(created_timestamp, (uint64_t)0)
      KV_SERIALIZE_OPT(updated_timestamp, (uint64_t)0)
    END_KV_SERIALIZE_MAP()
  };

  struct reward_split_result
  {
    uint64_t miner_reward = 0;
    uint64_t validator_reward = 0;
    uint64_t tail_emission = 0;
    uint64_t tx_fees = 0;
  };

  struct deregistration_penalty_criteria
  {
    uint16_t warn_threshold_bps = 0;
    uint16_t penalty_threshold_bps = 0;
    uint16_t deregister_threshold_bps = 0;
    uint64_t dereg_finality_depth = 0;
  };

  enum class duty_enforcement_state : uint8_t
  {
    none = 0,
    warning = 1,
    penalty = 2,
    deregistered_pending_finality = 3,
    deregistered = 4,
  };

  struct duty_enforcement_result
  {
    duty_enforcement_state state = duty_enforcement_state::none;
    uint16_t haircut_bps = 0;
  };

  struct deregistration_proof
  {
    std::string validator_id;
    uint64_t epoch = 0;
    uint64_t duty_slot = 0;
    uint8_t reason_code = 0;
    uint64_t evidence_height = 0;
    std::vector<std::string> signatures;

    bool is_well_formed(size_t min_signatures, std::string* reason = nullptr) const;
  };

  struct heartbeat_proof
  {
    std::string validator_id;
    uint64_t epoch = 0;
    uint64_t timestamp = 0;
    std::string signature;

    bool is_well_formed(std::string* reason = nullptr) const;
  };

  struct relay_challenge_proof
  {
    std::string validator_id;
    crypto::hash challenge = crypto::null_hash;
    crypto::hash response = crypto::null_hash;
    std::string signature;

    bool is_well_formed(std::string* reason = nullptr) const;
  };

  std::vector<bonded_validator_info> select_active_validator_set(
      const std::vector<bonded_validator_info>& validators,
      uint64_t epoch,
      uint64_t epoch_start_height,
      size_t active_count,
      const crypto::hash& chain_randomness,
      uint64_t min_collateral,
      uint64_t min_remaining_lock_blocks,
      uint64_t reward_activation_delay_blocks,
      uint32_t missed_duties_threshold);

  uint64_t compute_reward_eligible_height(
      uint64_t registration_height,
      uint64_t reward_activation_delay_blocks);

  bool validator_is_reward_eligible_at_height(
      const bonded_validator_info& validator,
      uint64_t height,
      uint64_t min_remaining_lock_blocks,
      uint64_t reward_activation_delay_blocks,
      uint32_t missed_duties_threshold);

  reward_split_result compute_reward_split(
      uint64_t base_block_reward,
      uint64_t tx_fees,
      uint64_t tail_emission,
      uint16_t validator_basis_points);

  bool validator_is_penalized(uint32_t missed_duties, uint32_t missed_duties_threshold);

  bool validator_is_reward_eligible(const bonded_validator_info& validator, uint32_t missed_duties_threshold);

  bool penalty_criteria_is_valid(
      const deregistration_penalty_criteria& criteria,
      std::string* reason = nullptr);

  uint16_t compute_miss_ratio_bps(uint64_t assigned_duties, uint64_t missed_duties);

  duty_enforcement_result evaluate_duty_enforcement(
      uint16_t miss_ratio_bps,
      uint64_t confirmations,
      const deregistration_penalty_criteria& criteria);

  uint64_t compute_unlock_height(
      const bonded_validator_info& validator,
      uint64_t current_height,
      uint64_t unlock_delay,
      bool slashing_enabled,
      uint16_t slash_basis_points);

  std::string make_deregistration_proof_key(const deregistration_proof& proof);

  bool signatures_are_canonical_and_unique(const std::vector<std::string>& signatures);

  bool proof_conflicts_with_observed_history(
      const std::string& proof_key,
      const crypto::hash& proof_digest,
      const std::set<std::string>& observed_keys,
      const std::vector<std::pair<std::string, crypto::hash>>& observed_equivocations);

  bool bonded_validator_reward_tier_is_enabled(
      network_type nettype,
      uint8_t hf_version);

  bool bonded_validator_registration_tier_is_enabled(
      network_type nettype,
      uint8_t hf_version);
}
