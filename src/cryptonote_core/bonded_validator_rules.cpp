#include "bonded_validator_rules.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "string_tools.h"

namespace
{
  template <typename T>
  bool add_overflow(T a, T b, T& out)
  {
    if (std::numeric_limits<T>::max() - a < b)
      return true;
    out = a + b;
    return false;
  }

  crypto::hash derive_selection_hash(const cryptonote::bonded_validator_info& v, uint64_t epoch, const crypto::hash& randomness)
  {
    std::string seed;
    seed.reserve(v.id.size() + sizeof(epoch) + sizeof(randomness));
    seed.append(v.id);
    seed.append(reinterpret_cast<const char*>(&epoch), sizeof(epoch));
    seed.append(reinterpret_cast<const char*>(&randomness), sizeof(randomness));
    return crypto::cn_fast_hash(seed.data(), seed.size());
  }

  bool non_empty_trimmed(const std::string& value)
  {
    return !epee::string_tools::trim(value).empty();
  }
}

namespace cryptonote
{
  bool collateral_registration_tx_payload::is_valid(std::string* reason) const
  {
    if (collateral_amount == 0)
    {
      if (reason) *reason = "collateral amount must be > 0";
      return false;
    }
    if (min_lock_blocks == 0)
    {
      if (reason) *reason = "minimum lock term must be > 0";
      return false;
    }
    if (!non_empty_trimmed(operator_key))
    {
      if (reason) *reason = "operator key is required";
      return false;
    }
    if (service_endpoints.empty())
    {
      if (reason) *reason = "at least one service endpoint is required";
      return false;
    }
    for (const std::string& endpoint : service_endpoints)
    {
      if (!non_empty_trimmed(endpoint))
      {
        if (reason) *reason = "service endpoint entries must be non-empty";
        return false;
      }
    }
    if (metadata_commitment == crypto::null_hash)
    {
      if (reason) *reason = "metadata commitment must be set";
      return false;
    }
    return true;
  }

  bool deregistration_proof::is_well_formed(size_t min_signatures, std::string* reason) const
  {
    if (!non_empty_trimmed(validator_id))
    {
      if (reason) *reason = "validator id missing";
      return false;
    }
    if (evidence_height == 0)
    {
      if (reason) *reason = "evidence height must be > 0";
      return false;
    }
    if (signatures.size() < min_signatures)
    {
      if (reason) *reason = "not enough signatures";
      return false;
    }
    for (const auto& signature : signatures)
    {
      if (!non_empty_trimmed(signature))
      {
        if (reason) *reason = "empty signature not allowed";
        return false;
      }
    }
    return true;
  }

  bool heartbeat_proof::is_well_formed(std::string* reason) const
  {
    if (!non_empty_trimmed(validator_id))
    {
      if (reason) *reason = "validator id missing";
      return false;
    }
    if (epoch == 0)
    {
      if (reason) *reason = "epoch must be > 0";
      return false;
    }
    if (timestamp == 0)
    {
      if (reason) *reason = "timestamp must be > 0";
      return false;
    }
    if (!non_empty_trimmed(signature))
    {
      if (reason) *reason = "signature missing";
      return false;
    }
    return true;
  }

  bool relay_challenge_proof::is_well_formed(std::string* reason) const
  {
    if (!non_empty_trimmed(validator_id))
    {
      if (reason) *reason = "validator id missing";
      return false;
    }
    if (challenge == crypto::null_hash || response == crypto::null_hash)
    {
      if (reason) *reason = "challenge/response hash must be non-null";
      return false;
    }
    if (!non_empty_trimmed(signature))
    {
      if (reason) *reason = "signature missing";
      return false;
    }
    return true;
  }

  std::vector<bonded_validator_info> select_active_validator_set(
      const std::vector<bonded_validator_info>& validators,
      uint64_t epoch,
      size_t active_count,
      const crypto::hash& chain_randomness,
      uint64_t min_collateral,
      uint64_t min_remaining_lock_blocks)
  {
    std::vector<bonded_validator_info> eligible;
    eligible.reserve(validators.size());

    for (const auto& validator : validators)
    {
      if (validator.deregistered || validator.collateral_amount < min_collateral)
        continue;
      if (validator.lock_end_height < epoch || (validator.lock_end_height - epoch) < min_remaining_lock_blocks)
        continue;
      eligible.push_back(validator);
    }

    std::sort(eligible.begin(), eligible.end(), [&](const bonded_validator_info& left, const bonded_validator_info& right) {
      const crypto::hash left_hash = derive_selection_hash(left, epoch, chain_randomness);
      const crypto::hash right_hash = derive_selection_hash(right, epoch, chain_randomness);
      const int cmp = std::memcmp(&left_hash, &right_hash, sizeof(crypto::hash));
      if (cmp != 0)
        return cmp < 0;
      return left.id < right.id;
    });

    if (eligible.size() > active_count)
      eligible.resize(active_count);

    return eligible;
  }

  reward_split_result compute_reward_split(
      uint64_t base_block_reward,
      uint64_t tx_fees,
      uint64_t tail_emission,
      uint16_t validator_basis_points)
  {
    reward_split_result result{};
    result.tx_fees = tx_fees;
    result.tail_emission = tail_emission;

    if (validator_basis_points > 10000)
      validator_basis_points = 10000;

    const uint64_t validator_from_base = (base_block_reward * validator_basis_points) / 10000;
    result.validator_reward = validator_from_base;
    result.miner_reward = base_block_reward - validator_from_base + tx_fees;

    // Explicitly preserve total emission invariants (base + tail + fees).
    const uint64_t expected_total = base_block_reward + tail_emission + tx_fees;
    uint64_t actual_total = 0;
    if (add_overflow(result.miner_reward, result.validator_reward, actual_total) ||
        add_overflow(actual_total, tail_emission, actual_total))
    {
      // Saturate to preserve safety on overflow.
      result.miner_reward = expected_total;
      result.validator_reward = 0;
    }

    return result;
  }

  bool validator_is_penalized(uint32_t missed_duties, uint32_t missed_duties_threshold)
  {
    return missed_duties_threshold > 0 && missed_duties >= missed_duties_threshold;
  }

  uint64_t compute_unlock_height(
      const bonded_validator_info& validator,
      uint64_t current_height,
      uint64_t unlock_delay,
      bool slashing_enabled,
      uint16_t slash_basis_points)
  {
    uint64_t base_unlock = std::max(validator.lock_end_height, current_height);
    if (add_overflow(base_unlock, unlock_delay, base_unlock))
      return std::numeric_limits<uint64_t>::max();

    if (!slashing_enabled || slash_basis_points == 0)
      return base_unlock;

    // Slashing enabled: add a deterministic cooldown proportional to slash severity.
    const uint64_t slash_extra_delay = (unlock_delay * slash_basis_points) / 10000;
    if (add_overflow(base_unlock, slash_extra_delay, base_unlock))
      return std::numeric_limits<uint64_t>::max();

    return base_unlock;
  }
}
