// Copyright (c) 2026, The Monero Project

#include "gtest/gtest.h"

#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/tx_extra.h"

TEST(TxExtraMvm, add_and_get_roundtrip)
{
  std::vector<uint8_t> extra;
  cryptonote::tx_extra_mvm_contract expected{};
  expected.version = cryptonote::TX_EXTRA_MVM_CONTRACT_VERSION;
  expected.action = "create_token";
  expected.contract_id = "msc_0123456789abcdef0123456789abcdef01234567";
  expected.code_hash = "abc123";
  expected.token_symbol = "USDT";
  expected.token_name = "TetherMonero";
  expected.token_supply = 1000000000000;
  expected.token_decimals = 6;
  expected.bytecode_hex = "6001600201";
  expected.monero_txid = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  expected.has_monero_block_height = true;
  expected.monero_block_height = 3456789;

  ASSERT_TRUE(cryptonote::add_mvm_contract_to_tx_extra(extra, expected));

  cryptonote::tx_extra_mvm_contract parsed{};
  ASSERT_TRUE(cryptonote::get_mvm_contract_from_tx_extra(extra, parsed));
  ASSERT_EQ(parsed.version, expected.version);
  ASSERT_EQ(parsed.action, expected.action);
  ASSERT_EQ(parsed.contract_id, expected.contract_id);
  ASSERT_EQ(parsed.code_hash, expected.code_hash);
  ASSERT_EQ(parsed.token_symbol, expected.token_symbol);
  ASSERT_EQ(parsed.token_name, expected.token_name);
  ASSERT_EQ(parsed.token_supply, expected.token_supply);
  ASSERT_EQ(parsed.token_decimals, expected.token_decimals);
  ASSERT_EQ(parsed.bytecode_hex, expected.bytecode_hex);
  ASSERT_EQ(parsed.monero_txid, expected.monero_txid);
  ASSERT_EQ(parsed.has_monero_block_height, expected.has_monero_block_height);
  ASSERT_EQ(parsed.monero_block_height, expected.monero_block_height);
}

TEST(TxExtraMvm, rejects_empty_fields)
{
  std::vector<uint8_t> extra;
  cryptonote::tx_extra_mvm_contract invalid{};
  invalid.version = cryptonote::TX_EXTRA_MVM_CONTRACT_VERSION;
  invalid.action = "";
  invalid.contract_id = "";
  invalid.code_hash = "hash";
  invalid.bytecode_hex = "6001";

  ASSERT_FALSE(cryptonote::add_mvm_contract_to_tx_extra(extra, invalid));
}

TEST(TxExtraMvm, transfer_requires_from_to_and_amount)
{
  std::vector<uint8_t> extra;
  cryptonote::tx_extra_mvm_contract invalid{};
  invalid.version = cryptonote::TX_EXTRA_MVM_CONTRACT_VERSION;
  invalid.action = "transfer_token";
  invalid.contract_id = "msc_transfer";
  invalid.code_hash = "abc123";
  invalid.token_symbol = "USDT";
  invalid.token_from = "alice";
  invalid.token_to = "bob";
  invalid.token_amount = 0;

  ASSERT_FALSE(cryptonote::add_mvm_contract_to_tx_extra(extra, invalid));

  invalid.token_amount = 100;
  ASSERT_TRUE(cryptonote::add_mvm_contract_to_tx_extra(extra, invalid));

  invalid.token_from = "alice";
  invalid.token_to = "alice";
  ASSERT_FALSE(cryptonote::add_mvm_contract_to_tx_extra(extra, invalid));
}

TEST(TxExtraMvm, create_contract_requires_bytecode)
{
  std::vector<uint8_t> extra;
  cryptonote::tx_extra_mvm_contract invalid{};
  invalid.version = cryptonote::TX_EXTRA_MVM_CONTRACT_VERSION;
  invalid.action = "create_contract";
  invalid.contract_id = "mvmc_test";
  invalid.code_hash = "abc123";
  invalid.bytecode_hex = "";
  ASSERT_FALSE(cryptonote::add_mvm_contract_to_tx_extra(extra, invalid));

  invalid.bytecode_hex = "6001600201";
  ASSERT_TRUE(cryptonote::add_mvm_contract_to_tx_extra(extra, invalid));
}

TEST(TxExtraMvm, supply_and_amount_have_upper_bounds)
{
  std::vector<uint8_t> extra;
  cryptonote::tx_extra_mvm_contract invalid{};
  invalid.version = cryptonote::TX_EXTRA_MVM_CONTRACT_VERSION;
  invalid.action = "create_token";
  invalid.contract_id = "mvmc_supply";
  invalid.code_hash = "abc123";
  invalid.token_symbol = "USDT";
  invalid.token_supply = cryptonote::TX_EXTRA_MVM_TOKEN_SUPPLY_MAX + 1;
  invalid.bytecode_hex = "6001600201";
  ASSERT_FALSE(cryptonote::add_mvm_contract_to_tx_extra(extra, invalid));

  invalid.token_supply = cryptonote::TX_EXTRA_MVM_TOKEN_SUPPLY_MAX;
  ASSERT_TRUE(cryptonote::add_mvm_contract_to_tx_extra(extra, invalid));
}

TEST(TxExtraMvm, rejects_invalid_monero_txid)
{
  std::vector<uint8_t> extra;
  cryptonote::tx_extra_mvm_contract invalid{};
  invalid.version = cryptonote::TX_EXTRA_MVM_CONTRACT_VERSION;
  invalid.action = "create_token";
  invalid.contract_id = "mvmc_invalid_txid";
  invalid.code_hash = "abc123";
  invalid.token_symbol = "USDT";
  invalid.token_supply = 1000;
  invalid.bytecode_hex = "6001600201";
  invalid.monero_txid = "xyz-not-hex";
  ASSERT_FALSE(cryptonote::add_mvm_contract_to_tx_extra(extra, invalid));
}
