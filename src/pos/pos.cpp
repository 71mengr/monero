#include "pos.h"

#include <algorithm>
#include <cstring>
#include <numeric>
#include <random>
#include <tuple>

#include "epee/string_tools.h"

namespace pos
{

namespace
{
std::string make_delegation_key(const crypto::public_key &delegator_key, const crypto::public_key &validator_key)
{
  return epee::string_tools::pod_to_hex(delegator_key) + ":" + epee::string_tools::pod_to_hex(validator_key);
}

uint64_t seed_to_u64(const crypto::hash &seed)
{
  uint64_t out = 0;
  std::memcpy(&out, seed.data, sizeof(out));
  return out;
}

uint64_t slash_percent_for_offense(uint8_t offense_type)
{
  switch (offense_type)
  {
    case 1: return 100;
    case 2: return 5;
    case 3: return 25;
    default: return 0;
  }
}
} // namespace

std::string pos_manager::key_to_string(const crypto::public_key &key)
{
  return epee::string_tools::pod_to_hex(key);
}

bool pos_manager::register_validator(const crypto::public_key &validator_key, uint64_t self_stake, uint64_t height)
{
  if (self_stake < MIN_VALIDATOR_STAKE)
    return false;

  const std::string key = key_to_string(validator_key);
  auto &record = m_validators[key];
  if (record.self_stake != 0)
    return false;

  record.key = validator_key;
  record.self_stake = self_stake;
  record.delegated_stake = 0;
  record.total_stake = self_stake;
  record.registered_height = height;
  record.last_active_height = height;
  record.is_active = true;
  return true;
}

bool pos_manager::unregister_validator(const crypto::public_key &validator_key)
{
  const std::string key = key_to_string(validator_key);
  auto it = m_validators.find(key);
  if (it == m_validators.end())
    return false;

  it->second.is_active = false;
  return true;
}

bool pos_manager::add_delegation(const crypto::public_key &delegator_key, const crypto::public_key &validator_key, uint64_t amount, uint64_t height, uint64_t lock_blocks)
{
  if (amount == 0)
    return false;

  const std::string validator_map_key = key_to_string(validator_key);
  auto vit = m_validators.find(validator_map_key);
  if (vit == m_validators.end() || !vit->second.is_active)
    return false;

  const std::string delegation_key = make_delegation_key(delegator_key, validator_key);
  auto &delegation = m_delegations[delegation_key];
  if (delegation.is_active)
    return false;

  delegation.delegator_key = delegator_key;
  delegation.validator_key = validator_key;
  delegation.amount = amount;
  delegation.created_height = height;
  delegation.unlock_height = height + lock_blocks;
  delegation.is_active = true;

  vit->second.delegated_stake += amount;
  vit->second.total_stake += amount;
  return true;
}

bool pos_manager::remove_delegation(const crypto::public_key &delegator_key, const crypto::public_key &validator_key, uint64_t height)
{
  const std::string delegation_key = make_delegation_key(delegator_key, validator_key);
  auto dit = m_delegations.find(delegation_key);
  if (dit == m_delegations.end() || !dit->second.is_active)
    return false;

  if (height < dit->second.unlock_height)
    return false;

  const std::string validator_map_key = key_to_string(validator_key);
  auto vit = m_validators.find(validator_map_key);
  if (vit != m_validators.end())
  {
    const uint64_t released = std::min(vit->second.delegated_stake, dit->second.amount);
    vit->second.delegated_stake -= released;
    vit->second.total_stake -= std::min(vit->second.total_stake, released);
  }

  dit->second.is_active = false;
  return true;
}

crypto::public_key pos_manager::select_block_producer(uint64_t height) const
{
  if (m_active_validator_order.empty())
    return crypto::public_key{};

  const size_t index = static_cast<size_t>(height % m_active_validator_order.size());
  return m_active_validator_order[index];
}

bool pos_manager::verify_block_signature(const crypto::hash &block_id, const crypto::signature &signature, const crypto::public_key &expected_validator) const
{
  return crypto::check_signature(block_id, expected_validator, signature);
}

void pos_manager::process_epoch_end(uint64_t height)
{
  crypto::hash seed{};
  const std::string seed_material = "dpos-epoch:" + std::to_string(height);
  crypto::cn_fast_hash(seed_material.data(), seed_material.size(), seed);
  update_validator_order(seed);
}

void pos_manager::update_validator_order(const crypto::hash &vrf_seed)
{
  std::vector<std::reference_wrapper<const validator_record>> ranked;
  ranked.reserve(m_validators.size());

  for (const auto &entry : m_validators)
  {
    if (entry.second.is_active)
      ranked.emplace_back(entry.second);
  }

  std::sort(ranked.begin(), ranked.end(), [](const validator_record &a, const validator_record &b) {
    return std::tie(b.total_stake, b.self_stake, b.registered_height) < std::tie(a.total_stake, a.self_stake, a.registered_height);
  });

  if (ranked.size() > MAX_ACTIVE_VALIDATORS)
    ranked.resize(MAX_ACTIVE_VALIDATORS);

  std::vector<crypto::public_key> ordered;
  ordered.reserve(ranked.size());
  for (const validator_record &record : ranked)
    ordered.push_back(record.key);

  std::mt19937_64 prng(seed_to_u64(vrf_seed));
  std::shuffle(ordered.begin(), ordered.end(), prng);
  m_active_validator_order = std::move(ordered);
}

uint64_t pos_manager::slash_validator(const crypto::public_key &validator_key, uint8_t offense_type)
{
  const std::string key = key_to_string(validator_key);
  auto it = m_validators.find(key);
  if (it == m_validators.end())
    return 0;

  const uint64_t slash_percent = slash_percent_for_offense(offense_type);
  const uint64_t slash_amount = (it->second.total_stake * slash_percent) / 100;

  it->second.total_stake -= std::min(it->second.total_stake, slash_amount);
  if (slash_percent == 100)
    it->second.is_active = false;

  if (it->second.total_stake <= it->second.self_stake)
  {
    it->second.delegated_stake = 0;
    it->second.self_stake = it->second.total_stake;
  }
  else
  {
    it->second.delegated_stake = it->second.total_stake - it->second.self_stake;
  }

  return slash_amount;
}

void pos_manager::finalize_checkpoint(uint64_t height, const crypto::hash &block_id)
{
  m_finalized_checkpoints[height] = block_id;
}

bool pos_manager::is_height_finalized(uint64_t height) const
{
  auto it = m_finalized_checkpoints.upper_bound(height);
  return it != m_finalized_checkpoints.begin();
}

std::vector<validator_record> pos_manager::get_active_validators() const
{
  std::vector<validator_record> out;
  out.reserve(m_active_validator_order.size());

  for (const auto &validator_key : m_active_validator_order)
  {
    const std::string key = key_to_string(validator_key);
    auto it = m_validators.find(key);
    if (it != m_validators.end() && it->second.is_active)
      out.push_back(it->second);
  }

  return out;
}

uint64_t pos_manager::get_total_stake() const
{
  uint64_t sum = 0;
  for (const auto &entry : m_validators)
  {
    if (entry.second.is_active)
      sum += entry.second.total_stake;
  }
  return sum;
}

} // namespace pos
