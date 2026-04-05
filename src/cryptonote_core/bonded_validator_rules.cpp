#include "bonded_validator_rules.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>

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
    static constexpr const char* domain = "bonded-validator-epoch-v1";
    std::string seed;
    seed.reserve(std::strlen(domain) + v.id.size() + sizeof(epoch) + sizeof(randomness));
    seed.append(domain);
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
  uint64_t compute_reward_eligible_height(
      uint64_t registration_height,
      uint64_t reward_activation_delay_blocks)
  {
    uint64_t eligible_height = 0;
    if (add_overflow(registration_height, reward_activation_delay_blocks, eligible_height))
      return std::numeric_limits<uint64_t>::max();
    return eligible_height;
  }

  bool validator_is_reward_eligible_at_height(
      const bonded_validator_info& validator,
      uint64_t height,
      uint64_t min_remaining_lock_blocks,
      uint64_t reward_activation_delay_blocks,
      uint32_t missed_duties_threshold)
  {
    if (validator.deregistered || !validator.active)
      return false;
    if (validator.collateral_amount == 0)
      return false;

    const uint64_t eligible_height = compute_reward_eligible_height(
        validator.registration_height, reward_activation_delay_blocks);
    if (eligible_height > height)
      return false;

    if (validator.lock_end_height < height || (validator.lock_end_height - height) < min_remaining_lock_blocks)
      return false;

    return !validator_is_penalized(validator.missed_duties + validator.penalty_points, missed_duties_threshold);
  }

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
    if (service_endpoints.size() > 1)
    {
      if (reason) *reason = "exactly one service endpoint is allowed per validator";
      return false;
    }
    std::unordered_set<std::string> normalized_endpoints;
    normalized_endpoints.reserve(service_endpoints.size());
    for (const std::string& endpoint : service_endpoints)
    {
      const std::string trimmed = epee::string_tools::trim(endpoint);
      if (trimmed.empty())
      {
        if (reason) *reason = "service endpoint entries must be non-empty";
        return false;
      }
      if (!normalized_endpoints.insert(trimmed).second)
      {
        if (reason) *reason = "duplicate service endpoint entries are not allowed";
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
    if (epoch == 0)
    {
      if (reason) *reason = "epoch must be > 0";
      return false;
    }
    if (evidence_height == 0)
    {
      if (reason) *reason = "evidence height must be > 0";
      return false;
    }
    if (!signatures_are_canonical_and_unique(signatures))
    {
      if (reason) *reason = "signatures must be non-empty, unique, and lexicographically sorted";
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
      uint64_t epoch_start_height,
      size_t active_count,
      const crypto::hash& chain_randomness,
      uint64_t min_collateral,
      uint64_t min_remaining_lock_blocks,
      uint64_t reward_activation_delay_blocks,
      uint32_t missed_duties_threshold)
  {
    std::vector<bonded_validator_info> eligible;
    eligible.reserve(validators.size());

    for (const auto& validator : validators)
    {
      if (validator.collateral_amount < min_collateral)
        continue;
      if (!validator_is_reward_eligible_at_height(
              validator,
              epoch_start_height,
              min_remaining_lock_blocks,
              reward_activation_delay_blocks,
              missed_duties_threshold))
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

    std::vector<bonded_validator_info> unique;
    unique.reserve(eligible.size());
    std::unordered_set<std::string> seen_ids;
    seen_ids.reserve(eligible.size());
    for (const auto& validator : eligible)
    {
      if (seen_ids.insert(validator.id).second)
        unique.push_back(validator);
    }

    return unique;
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
    const uint64_t miner_from_base = base_block_reward - validator_from_base;

    result.validator_reward = validator_from_base;
    result.miner_reward = miner_from_base;

    // Explicitly preserve total emission invariants (base + tail + fees).
    const uint64_t expected_total = base_block_reward + tail_emission + tx_fees;
    uint64_t actual_total = 0;
    if (add_overflow(result.miner_reward, tx_fees, actual_total) ||
        add_overflow(actual_total, result.validator_reward, actual_total) ||
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

  bool validator_is_reward_eligible(const bonded_validator_info& validator, uint32_t missed_duties_threshold)
  {
    if (!validator.active || validator.deregistered)
      return false;

    return !validator_is_penalized(validator.missed_duties + validator.penalty_points, missed_duties_threshold);
  }

  bool penalty_criteria_is_valid(const deregistration_penalty_criteria& criteria, std::string* reason)
  {
    const bool ordered = criteria.warn_threshold_bps <= criteria.penalty_threshold_bps &&
                         criteria.penalty_threshold_bps <= criteria.deregister_threshold_bps &&
                         criteria.deregister_threshold_bps <= 10000;
    if (!ordered)
    {
      if (reason) *reason = "penalty thresholds must satisfy warn <= penalty <= deregister <= 10000";
      return false;
    }
    return true;
  }

  uint16_t compute_miss_ratio_bps(uint64_t assigned_duties, uint64_t missed_duties)
  {
    if (assigned_duties == 0 || missed_duties == 0)
      return 0;

    if (missed_duties >= assigned_duties)
      return 10000;

    return static_cast<uint16_t>((10000ULL * missed_duties) / assigned_duties);
  }

  duty_enforcement_result evaluate_duty_enforcement(
      uint16_t miss_ratio_bps,
      uint64_t confirmations,
      const deregistration_penalty_criteria& criteria)
  {
    duty_enforcement_result result{};
    if (miss_ratio_bps < criteria.warn_threshold_bps)
      return result;

    if (miss_ratio_bps < criteria.penalty_threshold_bps)
    {
      result.state = duty_enforcement_state::warning;
      return result;
    }

    if (miss_ratio_bps < criteria.deregister_threshold_bps)
    {
      result.state = duty_enforcement_state::penalty;
      const uint16_t penalty_floor = criteria.penalty_threshold_bps;
      const uint16_t penalty_span = criteria.deregister_threshold_bps - penalty_floor;
      if (penalty_span == 0)
      {
        result.haircut_bps = 10000;
        return result;
      }

      const uint16_t distance = miss_ratio_bps - penalty_floor;
      result.haircut_bps = static_cast<uint16_t>(
          (static_cast<uint32_t>(distance) * 10000U) / penalty_span);
      return result;
    }

    result.state = confirmations >= criteria.dereg_finality_depth
                       ? duty_enforcement_state::deregistered
                       : duty_enforcement_state::deregistered_pending_finality;
    result.haircut_bps = 10000;
    return result;
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

  std::string make_deregistration_proof_key(const deregistration_proof& proof)
  {
    std::string key;
    key.reserve(proof.validator_id.size() + 48);
    key.append(proof.validator_id);
    key.push_back(':');
    key.append(std::to_string(proof.epoch));
    key.push_back(':');
    key.append(std::to_string(proof.duty_slot));
    key.push_back(':');
    key.append(std::to_string(proof.reason_code));
    return key;
  }

  bool signatures_are_canonical_and_unique(const std::vector<std::string>& signatures)
  {
    if (signatures.empty())
      return false;

    for (size_t i = 0; i < signatures.size(); ++i)
    {
      if (!non_empty_trimmed(signatures[i]))
        return false;
      if (i > 0 && signatures[i - 1] >= signatures[i])
        return false;
    }

    return true;
  }

  bool proof_conflicts_with_observed_history(
      const std::string& proof_key,
      const crypto::hash& proof_digest,
      const std::set<std::string>& observed_keys,
      const std::vector<std::pair<std::string, crypto::hash>>& observed_equivocations)
  {
    if (observed_keys.count(proof_key) > 0)
      return true; // Replay

    for (const auto& known : observed_equivocations)
    {
      if (known.first == proof_key && std::memcmp(&known.second, &proof_digest, sizeof(crypto::hash)) != 0)
        return true; // Equivocation
    }

    return false;
  }
}
