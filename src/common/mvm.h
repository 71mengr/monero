#pragma once

#include <string>
#include "cryptonote_config.h"

namespace tools
{
  constexpr uint64_t MVM_TOKEN_SUPPLY_MAX = 1000000000000000000ull;
  constexpr uint64_t MVM_TOKEN_AMOUNT_MAX = 1000000000000000000ull;

  bool normalize_mvm_bytecode_hex(const std::string &input, std::string &bytecode_hex);
  std::string derive_mvm_code_hash(const std::string &bytecode_hex);
  std::string derive_mvm_contract_id(const std::string &bytecode_hex, const std::string &salt = "");

  bool validate_mvm_supply_amount(uint64_t token_supply, uint64_t token_amount);
  bool validate_mvm_p2p_payload(const std::string &action, uint64_t total_received, uint64_t token_supply, uint64_t token_amount, const std::string &token_from, const std::string &token_to);
  bool validate_mvm_bytecode_hex_program(const std::string &bytecode_hex, std::string *error = nullptr);
  bool is_mvm_mainnet_enabled(cryptonote::network_type nettype);
}
