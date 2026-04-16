#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "crypto/crypto.h"
#include "crypto/hash.h"

namespace pos
{

constexpr uint64_t MIN_VALIDATOR_STAKE = 1000;
constexpr size_t MAX_ACTIVE_VALIDATORS = 50;
constexpr uint64_t EPOCH_LENGTH = 720;

struct validator_record
{
  crypto::public_key key{};
  uint64_t total_stake = 0;
  uint64_t self_stake = 0;
  uint64_t delegated_stake = 0;
  uint64_t last_active_height = 0;
  uint64_t registered_height = 0;
  uint64_t accumulated_rewards = 0;
  bool is_active = false;
};

struct delegation_record
{
  crypto::public_key delegator_key{};
  crypto::public_key validator_key{};
  uint64_t amount = 0;
  uint64_t created_height = 0;
  uint64_t unlock_height = 0;
  bool is_active = false;
};

class pos_manager
{
public:
  bool register_validator(const crypto::public_key &validator_key, uint64_t self_stake, uint64_t height);
  bool unregister_validator(const crypto::public_key &validator_key);

  bool add_delegation(const crypto::public_key &delegator_key, const crypto::public_key &validator_key, uint64_t amount, uint64_t height, uint64_t lock_blocks);
  bool remove_delegation(const crypto::public_key &delegator_key, const crypto::public_key &validator_key, uint64_t height);

  crypto::public_key select_block_producer(uint64_t height) const;
  bool verify_block_signature(const crypto::hash &block_id, const crypto::signature &signature, const crypto::public_key &expected_validator) const;
  void update_validator_activity(const crypto::public_key &validator_key, uint64_t height);

  void process_epoch_end(uint64_t height);
  void update_validator_order(const crypto::hash &vrf_seed);
  uint64_t slash_validator(const crypto::public_key &validator_key, uint8_t offense_type);

  void finalize_checkpoint(uint64_t height, const crypto::hash &block_id);
  bool is_height_finalized(uint64_t height) const;

  std::vector<validator_record> get_active_validators() const;
  const std::vector<crypto::public_key>& get_validator_order() const { return m_active_validator_order; }
  uint64_t get_next_turn_height(const crypto::public_key &validator_key, uint64_t current_height) const;
  uint64_t get_total_stake() const;
  uint64_t get_validator_stake(const crypto::public_key &validator_key) const;

private:
  static std::string key_to_string(const crypto::public_key &key);

  std::map<std::string, validator_record> m_validators;
  std::map<std::string, delegation_record> m_delegations;
  std::vector<crypto::public_key> m_active_validator_order;
  std::map<uint64_t, crypto::hash> m_finalized_checkpoints;
};

} // namespace pos
