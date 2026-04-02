#include <gtest/gtest.h>

#include "cryptonote_core/bonded_validator_rules.h"

namespace
{
  cryptonote::bonded_validator_info make_validator(const char* id, uint64_t collateral, uint64_t lock_end)
  {
    cryptonote::bonded_validator_info v{};
    v.id = id;
    v.collateral_amount = collateral;
    v.registration_height = 100;
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
  const auto set1 = cryptonote::select_active_validator_set(validators, 300, 300, 2, randomness, 1000, 100, 50, 10);
  const auto set2 = cryptonote::select_active_validator_set(validators, 300, 300, 2, randomness, 1000, 100, 50, 10);

  ASSERT_EQ(set1.size(), 2);
  ASSERT_EQ(set2.size(), 2);
  ASSERT_EQ(set1[0].id, set2[0].id);
  ASSERT_EQ(set1[1].id, set2[1].id);

  for (const auto& v : set1)
  {
    ASSERT_GE(v.collateral_amount, 1000);
    ASSERT_GE(v.lock_end_height - 300, 100);
    ASSERT_LE(v.registration_height + 50, 300);
  }
}

TEST(bonded_validator_rules, reward_eligible_height_is_registration_plus_delay)
{
  ASSERT_EQ(cryptonote::compute_reward_eligible_height(100, 25), 125);
  ASSERT_EQ(cryptonote::compute_reward_eligible_height(0, 0), 0);
}

TEST(bonded_validator_rules, reward_split_preserves_invariants)
{
  const auto split = cryptonote::compute_reward_split(
      10'000'000'000ULL,
      1'000'000ULL,
      600'000'000ULL,
      5000);

  ASSERT_EQ(split.validator_reward, 5'000'000'000ULL);
  ASSERT_EQ(split.miner_reward, 5'000'000'000ULL);

  const uint64_t total_paid = split.validator_reward + split.miner_reward + split.tx_fees + split.tail_emission;
  const uint64_t total_expected = 10'000'000'000ULL + 1'000'000ULL + 600'000'000ULL;
  ASSERT_EQ(total_paid, total_expected);
}

TEST(bonded_validator_rules, activation_delay_and_penalties_gate_active_set)
{
  auto eligible = make_validator("eligible", 2000, 1000);
  eligible.registration_height = 200;
  eligible.missed_duties = 1;
  eligible.penalty_points = 1;

  auto too_new = make_validator("too_new", 2000, 1000);
  too_new.registration_height = 280;

  auto penalized = make_validator("penalized", 2000, 1000);
  penalized.registration_height = 200;
  penalized.missed_duties = 7;
  penalized.penalty_points = 5;

  std::vector<cryptonote::bonded_validator_info> validators{eligible, too_new, penalized};
  const crypto::hash randomness = crypto::cn_fast_hash("epoch-seed-2", 12);

  const auto set = cryptonote::select_active_validator_set(
      validators,
      12,
      300,
      5,
      randomness,
      1000,
      50,
      50,
      10);

  ASSERT_EQ(set.size(), 1);
  ASSERT_EQ(set.front().id, "eligible");
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
  ASSERT_TRUE(cryptonote::validator_is_reward_eligible(eligible, 5));

  eligible.online = true;
  eligible.penalty_points = 4;
  ASSERT_FALSE(cryptonote::validator_is_reward_eligible(eligible, 5));

  eligible.penalty_points = 1;
  eligible.registration_height = 100;
  eligible.lock_end_height = 300;
  ASSERT_TRUE(cryptonote::validator_is_reward_eligible_at_height(eligible, 200, 50, 50, 5));
  ASSERT_FALSE(cryptonote::validator_is_reward_eligible_at_height(eligible, 140, 50, 50, 5));

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

TEST(bonded_validator_rules, deregistration_penalty_criteria_validation_and_miss_ratio)
{
  cryptonote::deregistration_penalty_criteria criteria{};
  criteria.warn_threshold_bps = 1000;
  criteria.penalty_threshold_bps = 2500;
  criteria.deregister_threshold_bps = 6000;
  criteria.dereg_finality_depth = 20;

  std::string reason;
  ASSERT_TRUE(cryptonote::penalty_criteria_is_valid(criteria, &reason)) << reason;

  criteria.penalty_threshold_bps = 900;
  ASSERT_FALSE(cryptonote::penalty_criteria_is_valid(criteria, &reason));

  ASSERT_EQ(cryptonote::compute_miss_ratio_bps(0, 10), 0);
  ASSERT_EQ(cryptonote::compute_miss_ratio_bps(100, 10), 1000);
  ASSERT_EQ(cryptonote::compute_miss_ratio_bps(100, 100), 10000);
  ASSERT_EQ(cryptonote::compute_miss_ratio_bps(100, 120), 10000);
}

TEST(bonded_validator_rules, evaluate_duty_enforcement_states)
{
  cryptonote::deregistration_penalty_criteria criteria{};
  criteria.warn_threshold_bps = 1000;
  criteria.penalty_threshold_bps = 2500;
  criteria.deregister_threshold_bps = 6000;
  criteria.dereg_finality_depth = 12;

  auto result = cryptonote::evaluate_duty_enforcement(500, 0, criteria);
  ASSERT_EQ(result.state, cryptonote::duty_enforcement_state::none);
  ASSERT_EQ(result.haircut_bps, 0);

  result = cryptonote::evaluate_duty_enforcement(1500, 0, criteria);
  ASSERT_EQ(result.state, cryptonote::duty_enforcement_state::warning);
  ASSERT_EQ(result.haircut_bps, 0);

  result = cryptonote::evaluate_duty_enforcement(4000, 0, criteria);
  ASSERT_EQ(result.state, cryptonote::duty_enforcement_state::penalty);
  ASSERT_GT(result.haircut_bps, 0);
  ASSERT_LT(result.haircut_bps, 10000);

  result = cryptonote::evaluate_duty_enforcement(6500, 11, criteria);
  ASSERT_EQ(result.state, cryptonote::duty_enforcement_state::deregistered_pending_finality);
  ASSERT_EQ(result.haircut_bps, 10000);

  result = cryptonote::evaluate_duty_enforcement(6500, 12, criteria);
  ASSERT_EQ(result.state, cryptonote::duty_enforcement_state::deregistered);
  ASSERT_EQ(result.haircut_bps, 10000);
}
