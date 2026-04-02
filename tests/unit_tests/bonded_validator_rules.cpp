#include <gtest/gtest.h>

#include "cryptonote_core/bonded_validator_rules.h"

namespace
{
  cryptonote::bonded_validator_info make_validator(const char* id, uint64_t collateral, uint64_t lock_end)
  {
    cryptonote::bonded_validator_info v{};
    v.id = id;
    v.collateral_amount = collateral;
    v.lock_end_height = lock_end;
    v.active = true;
    v.online = true;
    return v;
  }
}

TEST(bonded_validator_rules, collateral_payload_validation)
{
  cryptonote::collateral_registration_tx_payload payload{};
  payload.collateral_amount = 1000;
  payload.lock_start_height = 100;
  payload.min_lock_blocks = 720;
  payload.operator_key = "operator-pubkey";
  payload.service_endpoints = {"tcp://1.2.3.4:18080"};
  payload.metadata_commitment = crypto::cn_fast_hash("meta", 4);

  std::string reason;
  ASSERT_TRUE(payload.is_valid(&reason)) << reason;

  payload.service_endpoints = {"   "};
  ASSERT_FALSE(payload.is_valid(&reason));
}

TEST(bonded_validator_rules, deterministic_active_set_selection)
{
  std::vector<cryptonote::bonded_validator_info> validators{
      make_validator("a", 1000, 500),
      make_validator("b", 1200, 500),
      make_validator("c", 1500, 500),
      make_validator("d", 100, 500)};

  const crypto::hash randomness = crypto::cn_fast_hash("epoch-seed", 10);
  const auto set1 = cryptonote::select_active_validator_set(validators, 300, 2, randomness, 1000, 100);
  const auto set2 = cryptonote::select_active_validator_set(validators, 300, 2, randomness, 1000, 100);

  ASSERT_EQ(set1.size(), 2);
  ASSERT_EQ(set2.size(), 2);
  ASSERT_EQ(set1[0].id, set2[0].id);
  ASSERT_EQ(set1[1].id, set2[1].id);

  for (const auto& v : set1)
  {
    ASSERT_GE(v.collateral_amount, 1000);
    ASSERT_GE(v.lock_end_height - 300, 100);
  }
}

TEST(bonded_validator_rules, reward_split_preserves_invariants)
{
  const auto split = cryptonote::compute_reward_split(
      10'000'000'000ULL,
      1'000'000ULL,
      600'000'000ULL,
      5000);

  ASSERT_EQ(split.validator_reward, 5'000'000'000ULL);
  ASSERT_EQ(split.miner_reward, 5'001'000'000ULL);

  const uint64_t total_paid = split.validator_reward + split.miner_reward + split.tail_emission;
  const uint64_t total_expected = 10'000'000'000ULL + 1'000'000ULL + 600'000'000ULL;
  ASSERT_EQ(total_paid, total_expected);
}

TEST(bonded_validator_rules, penalty_and_unlock_rules)
{
  ASSERT_TRUE(cryptonote::validator_is_penalized(10, 10));
  ASSERT_FALSE(cryptonote::validator_is_penalized(9, 10));

  cryptonote::bonded_validator_info eligible{};
  eligible.active = true;
  eligible.online = true;
  eligible.missed_duties = 2;
  eligible.penalty_points = 1;
  ASSERT_TRUE(cryptonote::validator_is_reward_eligible(eligible, 5));

  eligible.online = false;
  ASSERT_FALSE(cryptonote::validator_is_reward_eligible(eligible, 5));

  eligible.online = true;
  eligible.penalty_points = 4;
  ASSERT_FALSE(cryptonote::validator_is_reward_eligible(eligible, 5));

  cryptonote::bonded_validator_info v{};
  v.lock_end_height = 1000;

  const uint64_t normal_unlock = cryptonote::compute_unlock_height(v, 900, 100, false, 0);
  ASSERT_EQ(normal_unlock, 1100);

  const uint64_t slashed_unlock = cryptonote::compute_unlock_height(v, 900, 100, true, 5000);
  ASSERT_EQ(slashed_unlock, 1150);
}

TEST(bonded_validator_rules, proof_formats)
{
  cryptonote::deregistration_proof dereg{"val-1", 123, {"sig1", "sig2"}};
  cryptonote::heartbeat_proof hb{"val-1", 10, 12345, "sig"};
  cryptonote::relay_challenge_proof relay{"val-1", crypto::cn_fast_hash("c", 1), crypto::cn_fast_hash("r", 1), "sig"};

  std::string reason;
  ASSERT_TRUE(dereg.is_well_formed(2, &reason)) << reason;
  ASSERT_TRUE(hb.is_well_formed(&reason)) << reason;
  ASSERT_TRUE(relay.is_well_formed(&reason)) << reason;

  dereg.signatures.clear();
  ASSERT_FALSE(dereg.is_well_formed(1, &reason));
}
