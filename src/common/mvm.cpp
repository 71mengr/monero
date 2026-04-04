#include "mvm.h"

#include <algorithm>
#include <cctype>

#include "util.h"
#include "string_tools.h"

namespace tools
{
  namespace
  {
    constexpr uint8_t OP_PUSH = 0x01;
    constexpr uint8_t OP_LOAD = 0x02;
    constexpr uint8_t OP_STORE = 0x03;
    constexpr uint8_t OP_ADD = 0x04;
    constexpr uint8_t OP_SUB = 0x05;
    constexpr uint8_t OP_MUL = 0x06;
    constexpr uint8_t OP_DIV = 0x07;
    constexpr uint8_t OP_EQ = 0x08;
    constexpr uint8_t OP_GT = 0x09;
    constexpr uint8_t OP_LT = 0x0A;
    constexpr uint8_t OP_DUP = 0x0B;
    constexpr uint8_t OP_SWAP = 0x0C;
    constexpr uint8_t OP_JUMP = 0x0D;
    constexpr uint8_t OP_JUMPI = 0x0E;
    constexpr uint8_t OP_BLOCK_HEIGHT = 0x0F;
    constexpr uint8_t OP_CREATE_CONTRACT = 0x10;
    constexpr uint8_t OP_CREATE_TOKEN = 0x11;
    constexpr uint8_t OP_MINT_TOKEN = 0x12;
    constexpr uint8_t OP_TRANSFER_TOKEN = 0x13;
    constexpr uint8_t OP_ASSERT = 0x14;
    constexpr uint8_t OP_EMIT = 0x15;
    constexpr uint8_t OP_HALT = 0xFF;

    bool read_str(const std::string &data, size_t &offset)
    {
      if (offset >= data.size())
        return false;
      const uint8_t len = static_cast<uint8_t>(data[offset]);
      offset += 1;
      if (offset + len > data.size())
        return false;
      offset += len;
      return true;
    }
  }

  bool normalize_mvm_bytecode_hex(const std::string &input, std::string &bytecode_hex)
  {
    bytecode_hex.clear();
    bytecode_hex.reserve(input.size());

    std::string raw = input;
    if (raw.rfind("0x", 0) == 0 || raw.rfind("0X", 0) == 0)
      raw = raw.substr(2);

    raw.erase(std::remove_if(raw.begin(), raw.end(), [](char c) {
      return c == ' ' || c == '\n' || c == '\r' || c == '\t';
    }), raw.end());

    if (raw.empty() || raw.size() % 2 != 0)
      return false;

    for (const char c : raw)
      if (!std::isxdigit(static_cast<unsigned char>(c)))
        return false;

    bytecode_hex = raw;
    return true;
  }

  std::string derive_mvm_code_hash(const std::string &bytecode_hex)
  {
    return make_unique_web3_id("mvm:code:" + bytecode_hex);
  }

  std::string derive_mvm_contract_id(const std::string &bytecode_hex)
  {
    return std::string("mvmc_") + make_unique_web3_id("mvm:contract:" + bytecode_hex).substr(2);
  }

  bool validate_mvm_supply_amount(uint64_t token_supply, uint64_t token_amount)
  {
    if (token_supply > MVM_TOKEN_SUPPLY_MAX)
      return false;
    if (token_amount > MVM_TOKEN_AMOUNT_MAX)
      return false;
    return true;
  }

  bool validate_mvm_p2p_payload(const std::string &action, uint64_t total_received, uint64_t token_supply, uint64_t token_amount, const std::string &token_from, const std::string &token_to)
  {
    if (!validate_mvm_supply_amount(token_supply, token_amount))
      return false;

    if (action == "transfer_token" && token_from == token_to)
      return false;

    const bool requires_received_amount =
      action == "create_contract" ||
      action == "create_token" ||
      action == "transfer_token";

    if (requires_received_amount && total_received == 0)
      return false;

    return true;
  }

  bool validate_mvm_bytecode_hex_program(const std::string &bytecode_hex, std::string *error)
  {
    std::string normalized;
    if (!normalize_mvm_bytecode_hex(bytecode_hex, normalized))
    {
      if (error) *error = "invalid hex";
      return false;
    }

    std::string data;
    if (!epee::string_tools::parse_hexstr_to_binbuff(normalized, data))
    {
      if (error) *error = "failed to decode hex";
      return false;
    }

    size_t offset = 0;
    while (offset < data.size())
    {
      const uint8_t op = static_cast<uint8_t>(data[offset++]);
      switch (op)
      {
        case OP_PUSH:
          if (offset + 8 > data.size()) { if (error) *error = "truncated PUSH"; return false; }
          offset += 8;
          break;
        case OP_LOAD:
        case OP_STORE:
        case OP_ASSERT:
          if (!read_str(data, offset)) { if (error) *error = "truncated string operand"; return false; }
          break;
        case OP_JUMP:
        case OP_JUMPI:
          if (offset + 2 > data.size()) { if (error) *error = "truncated jump target"; return false; }
          offset += 2;
          break;
        case OP_EMIT:
          if (!read_str(data, offset) || offset >= data.size()) { if (error) *error = "truncated emit operand"; return false; }
          offset += 1;
          break;
        case OP_CREATE_CONTRACT:
          if (!read_str(data, offset) || !read_str(data, offset) || !read_str(data, offset)) { if (error) *error = "truncated create_contract"; return false; }
          break;
        case OP_CREATE_TOKEN:
          if (!read_str(data, offset) || !read_str(data, offset) || !read_str(data, offset) || !read_str(data, offset) || offset + 9 > data.size()) { if (error) *error = "truncated create_token"; return false; }
          offset += 9;
          break;
        case OP_MINT_TOKEN:
          if (!read_str(data, offset) || !read_str(data, offset) || offset + 8 > data.size()) { if (error) *error = "truncated mint_token"; return false; }
          offset += 8;
          break;
        case OP_TRANSFER_TOKEN:
          if (!read_str(data, offset) || !read_str(data, offset) || !read_str(data, offset) || offset + 8 > data.size()) { if (error) *error = "truncated transfer_token"; return false; }
          offset += 8;
          break;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DIV:
        case OP_EQ:
        case OP_GT:
        case OP_LT:
        case OP_DUP:
        case OP_SWAP:
        case OP_BLOCK_HEIGHT:
        case OP_HALT:
          break;
        default:
          if (error) *error = "unknown opcode";
          return false;
      }
    }

    return true;
  }

  bool is_mvm_mainnet_enabled(cryptonote::network_type nettype)
  {
    return nettype == cryptonote::MAINNET;
  }
}
