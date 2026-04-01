#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "crypto/hash.h"

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
    uint64_t collateral_amount = 0;
    uint64_t registration_height = 0;
    uint64_t lock_end_height = 0;
    uint64_t last_uptime_proof_height = 0;
    uint32_t missed_duties = 0;
    bool deregistered = false;
  };

  struct reward_split_result
  {
    uint64_t miner_reward = 0;
    uint64_t validator_reward = 0;
    uint64_t tail_emission = 0;
    uint64_t tx_fees = 0;
  };

  struct deregistration_proof
  {
    std::string validator_id;
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
      size_t active_count,
      const crypto::hash& chain_randomness,
      uint64_t min_collateral,
      uint64_t min_remaining_lock_blocks);

  reward_split_result compute_reward_split(
      uint64_t base_block_reward,
      uint64_t tx_fees,
      uint64_t tail_emission,
      uint16_t validator_basis_points);

  bool validator_is_penalized(uint32_t missed_duties, uint32_t missed_duties_threshold);

  uint64_t compute_unlock_height(
      const bonded_validator_info& validator,
      uint64_t current_height,
      uint64_t unlock_delay,
      bool slashing_enabled,
      uint16_t slash_basis_points);
}
