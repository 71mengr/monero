#include <gtest/gtest.h>

#include "cryptonote_core/bonded_validator_rules.h"
#include "storages/portable_storage.h"

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

  payload.service_endpoints = {"tcp://1.2.3.4:18080", " tcp://1.2.3.4:18080 "};
  ASSERT_FALSE(payload.is_valid(&reason));

  payload.service_endpoints = {"tcp://1.2.3.4:18080", "tcp://5.6.7.8:18080"};
  ASSERT_FALSE(payload.is_valid(&reason));
}

TEST(bonded_validator_rules, bonded_validator_info_encoding_round_trip)
{
  cryptonote::bonded_validator_info original{};
  original.id = "validator-1";
  original.operator_key = "op-key";
  original.collateral_txid = "txid";
  original.collateral_amount = 123456789;
  original.registration_height = 222;
  original.lock_end_height = 999;
  original.last_uptime_proof_height = 345;
  original.missed_duties = 2;
  original.penalty_points = 1;
  original.active = true;
  original.online = true;
  original.deregistered = false;
  original.created_height = 200;
  original.updated_height = 300;
  original.created_timestamp = 1710000000;
  original.updated_timestamp = 1710001000;

  epee::serialization::portable_storage stg{};
  ASSERT_TRUE(original.store(stg));

  epee::byte_slice buffer{};
  ASSERT_TRUE(stg.store_to_binary(buffer));

  epee::serialization::portable_storage decoded_stg{};
  ASSERT_TRUE(decoded_stg.load_from_binary(epee::to_span(buffer)));

  cryptonote::bonded_validator_info decoded{};
  ASSERT_TRUE(decoded.load(decoded_stg));

  ASSERT_EQ(decoded.id, original.id);
  ASSERT_EQ(decoded.operator_key, original.operator_key);
  ASSERT_EQ(decoded.collateral_txid, original.collateral_txid);
  ASSERT_EQ(decoded.collateral_amount, original.collateral_amount);
  ASSERT_EQ(decoded.registration_height, original.registration_height);
  ASSERT_EQ(decoded.lock_end_height, original.lock_end_height);
  ASSERT_EQ(decoded.last_uptime_proof_height, original.last_uptime_proof_height);
  ASSERT_EQ(decoded.missed_duties, original.missed_duties);
  ASSERT_EQ(decoded.penalty_points, original.penalty_points);
  ASSERT_EQ(decoded.active, original.active);
  ASSERT_EQ(decoded.online, original.online);
  ASSERT_EQ(decoded.deregistered, original.deregistered);
  ASSERT_EQ(decoded.created_height, original.created_height);
  ASSERT_EQ(decoded.updated_height, original.updated_height);
  ASSERT_EQ(decoded.created_timestamp, original.created_timestamp);
  ASSERT_EQ(decoded.updated_timestamp, original.updated_timestamp);
}

TEST(bonded_validator_rules, mainnet_activation_gates)
{
  ASSERT_TRUE(cryptonote::bonded_validator_reward_tier_is_enabled(cryptonote::MAINNET, HF_VERSION_MASTERNODE_REWARD_SPLIT));
  ASSERT_FALSE(cryptonote::bonded_validator_reward_tier_is_enabled(cryptonote::MAINNET, HF_VERSION_MASTERNODE_REWARD_SPLIT - 1));
  ASSERT_FALSE(cryptonote::bonded_validator_reward_tier_is_enabled(cryptonote::TESTNET, HF_VERSION_MASTERNODE_REWARD_SPLIT));

  ASSERT_TRUE(cryptonote::bonded_validator_registration_tier_is_enabled(cryptonote::MAINNET, HF_MN_REG));
  ASSERT_FALSE(cryptonote::bonded_validator_registration_tier_is_enabled(cryptonote::MAINNET, HF_MN_REG - 1));
  ASSERT_FALSE(cryptonote::bonded_validator_registration_tier_is_enabled(cryptonote::STAGENET, HF_MN_REG));
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

TEST(bonded_validator_rules, active_set_deduplicates_validator_ids)
{
  auto one = make_validator("dup", 5000, 800);
  one.registration_height = 100;
  auto two = make_validator("dup", 6000, 900);
  two.registration_height = 100;
  auto three = make_validator("uniq", 7000, 900);
  three.registration_height = 100;

  std::vector<cryptonote::bonded_validator_info> validators{one, two, three};
  const crypto::hash randomness = crypto::cn_fast_hash("dedupe-seed", 11);

  const auto set = cryptonote::select_active_validator_set(
      validators, 10, 300, 3, randomness, 1000, 50, 50, 10);

  ASSERT_EQ(set.size(), 2);
  ASSERT_NE(set[0].id, set[1].id);
}

TEST(bonded_validator_rules, reward_split_accept_reject_vectors)
{
  const auto capped = cryptonote::compute_reward_split(1000, 10, 20, 10001);
  ASSERT_EQ(capped.validator_reward, 1000);
  ASSERT_EQ(capped.miner_reward, 0);

  const auto zero = cryptonote::compute_reward_split(1000, 10, 20, 0);
  ASSERT_EQ(zero.validator_reward, 0);
  ASSERT_EQ(zero.miner_reward, 1000);
}

TEST(bonded_validator_rules, active_set_reorg_rollback_is_deterministic)
{
  auto a = make_validator("a", 2000, 700);
  a.registration_height = 100;
  auto b = make_validator("b", 2000, 700);
  b.registration_height = 100;
  auto c = make_validator("c", 2000, 700);
  c.registration_height = 260;

  std::vector<cryptonote::bonded_validator_info> validators{a, b, c};
  const crypto::hash randomness = crypto::cn_fast_hash("reorg-seed", 10);

  const auto pre_reorg = cryptonote::select_active_validator_set(
      validators, 1, 240, 2, randomness, 1000, 50, 50, 10);
  const auto post_reorg = cryptonote::select_active_validator_set(
      validators, 1, 320, 2, randomness, 1000, 50, 50, 10);
  const auto rollback = cryptonote::select_active_validator_set(
      validators, 1, 240, 2, randomness, 1000, 50, 50, 10);

  ASSERT_EQ(pre_reorg.size(), rollback.size());
  ASSERT_EQ(pre_reorg[0].id, rollback[0].id);
  ASSERT_EQ(pre_reorg[1].id, rollback[1].id);

  ASSERT_EQ(post_reorg.size(), 2);
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

TEST(bonded_validator_rules, reward_split_preserves_invariants_across_basis_points)
{
  for (uint16_t basis_points : {0, 1, 2500, 5000, 9999, 10000, 12000})
  {
    const uint64_t base = 12'345'678'901ULL;
    const uint64_t fees = 998'877ULL;
    const uint64_t tail = 600'000'000ULL;
    const auto split = cryptonote::compute_reward_split(base, fees, tail, basis_points);

    const uint64_t paid = split.validator_reward + split.miner_reward + split.tx_fees + split.tail_emission;
    ASSERT_EQ(paid, base + fees + tail);
  }
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
  cryptonote::deregistration_proof dereg{};
  dereg.validator_id = "val-1";
  dereg.epoch = 12;
  dereg.duty_slot = 4;
  dereg.reason_code = 1;
  dereg.evidence_height = 123;
  dereg.signatures = {"sig1", "sig2"};
  cryptonote::heartbeat_proof hb{"val-1", 10, 12345, "sig"};
  cryptonote::relay_challenge_proof relay{"val-1", crypto::cn_fast_hash("c", 1), crypto::cn_fast_hash("r", 1), "sig"};

  std::string reason;
  ASSERT_TRUE(dereg.is_well_formed(2, &reason)) << reason;
  ASSERT_TRUE(hb.is_well_formed(&reason)) << reason;
  ASSERT_TRUE(relay.is_well_formed(&reason)) << reason;

  dereg.signatures.clear();
  ASSERT_FALSE(dereg.is_well_formed(1, &reason));
}

TEST(bonded_validator_rules, deregistration_proof_canonical_signature_ordering)
{
  cryptonote::deregistration_proof proof{};
  proof.validator_id = "val-ordering";
  proof.epoch = 20;
  proof.duty_slot = 2;
  proof.reason_code = 3;
  proof.evidence_height = 500;

  proof.signatures = {"sig-a", "sig-b", "sig-c"};
  std::string reason;
  ASSERT_TRUE(proof.is_well_formed(2, &reason)) << reason;

  proof.signatures = {"sig-b", "sig-a"};
  ASSERT_FALSE(proof.is_well_formed(2, &reason));

  proof.signatures = {"sig-a", "sig-a"};
  ASSERT_FALSE(proof.is_well_formed(2, &reason));
}

TEST(bonded_validator_rules, replay_and_equivocation_protection_for_validator_proofs)
{
  cryptonote::deregistration_proof proof{};
  proof.validator_id = "val-1";
  proof.epoch = 44;
  proof.duty_slot = 7;
  proof.reason_code = 9;
  proof.evidence_height = 999;
  proof.signatures = {"sig-a", "sig-b"};

  const std::string key = cryptonote::make_deregistration_proof_key(proof);
  const crypto::hash digest_a = crypto::cn_fast_hash("proof-A", 7);
  const crypto::hash digest_b = crypto::cn_fast_hash("proof-B", 7);

  std::set<std::string> observed_keys{};
  std::vector<std::pair<std::string, crypto::hash>> observed_equivocations{};

  ASSERT_FALSE(cryptonote::proof_conflicts_with_observed_history(key, digest_a, observed_keys, observed_equivocations));

  observed_keys.insert(key);
  ASSERT_TRUE(cryptonote::proof_conflicts_with_observed_history(key, digest_a, observed_keys, observed_equivocations));

  observed_keys.clear();
  observed_equivocations.push_back({key, digest_a});
  ASSERT_FALSE(cryptonote::proof_conflicts_with_observed_history(key, digest_a, observed_keys, observed_equivocations));
  ASSERT_TRUE(cryptonote::proof_conflicts_with_observed_history(key, digest_b, observed_keys, observed_equivocations));
}

TEST(bonded_validator_rules, replay_guard_reorg_rollback_allows_reacceptance)
{
  cryptonote::deregistration_proof proof{};
  proof.validator_id = "val-reorg";
  proof.epoch = 3;
  proof.duty_slot = 1;
  proof.reason_code = 2;
  proof.evidence_height = 77;
  proof.signatures = {"sig-a", "sig-b"};

  const std::string key = cryptonote::make_deregistration_proof_key(proof);
  const crypto::hash digest = crypto::cn_fast_hash("proof-reorg", 11);

  std::set<std::string> observed_keys{key};
  std::vector<std::pair<std::string, crypto::hash>> observed_equivocations{};

  ASSERT_TRUE(cryptonote::proof_conflicts_with_observed_history(key, digest, observed_keys, observed_equivocations));

  // Simulate a detach/reorg rollback that reverts the proof observation.
  observed_keys.erase(key);
  ASSERT_FALSE(cryptonote::proof_conflicts_with_observed_history(key, digest, observed_keys, observed_equivocations));
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
