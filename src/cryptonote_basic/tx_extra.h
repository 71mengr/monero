// Copyright (c) 2014-2022, The Monero Project
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#pragma once

#include <cctype>


#define TX_EXTRA_PADDING_MAX_COUNT          255
#define TX_EXTRA_NONCE_MAX_COUNT            255

#define TX_EXTRA_TAG_PADDING                0x00
#define TX_EXTRA_TAG_PUBKEY                 0x01
#define TX_EXTRA_NONCE                      0x02
#define TX_EXTRA_MERGE_MINING_TAG           0x03
#define TX_EXTRA_TAG_ADDITIONAL_PUBKEYS     0x04
#define TX_EXTRA_TAG_MASTERNODE_REGISTRATION 0x05
#define TX_EXTRA_TAG_MVM_CONTRACT           0x06
#define TX_EXTRA_MYSTERIOUS_MINERGATE_TAG   0xDE

#define TX_EXTRA_NONCE_PAYMENT_ID           0x00
#define TX_EXTRA_NONCE_ENCRYPTED_PAYMENT_ID 0x01

namespace cryptonote
{
  constexpr uint8_t TX_EXTRA_MASTERNODE_REGISTRATION_VERSION = 1;
  constexpr uint8_t TX_EXTRA_MVM_CONTRACT_VERSION = 2;
  constexpr uint8_t TX_EXTRA_MVM_CONTRACT_VERSION_MIN = 1;
  constexpr size_t TX_EXTRA_MASTERNODE_SERVICE_ENDPOINT_COMMITMENT_MAX_SIZE = 64;
  constexpr size_t TX_EXTRA_MVM_ACTION_MAX_SIZE = 32;
  constexpr size_t TX_EXTRA_MVM_CONTRACT_ID_MAX_SIZE = 64;
  constexpr size_t TX_EXTRA_MVM_CODE_HASH_MAX_SIZE = 64;
  constexpr size_t TX_EXTRA_MVM_TOKEN_SYMBOL_MAX_SIZE = 16;
  constexpr size_t TX_EXTRA_MVM_TOKEN_NAME_MAX_SIZE = 64;
  constexpr size_t TX_EXTRA_MVM_TOKEN_ADDRESS_MAX_SIZE = 128;
  constexpr size_t TX_EXTRA_MVM_BYTECODE_MAX_SIZE = 4096;
  constexpr size_t TX_EXTRA_MVM_TXID_MAX_SIZE = 64;
  constexpr uint64_t TX_EXTRA_MVM_TOKEN_SUPPLY_MAX = 1000000000000000000ull;
  constexpr uint64_t TX_EXTRA_MVM_TOKEN_AMOUNT_MAX = 1000000000000000000ull;

  struct masternode_collateral_outpoint
  {
    crypto::hash txid;
    uint32_t vout;

    BEGIN_SERIALIZE()
      FIELD(txid)
      VARINT_FIELD(vout)
    END_SERIALIZE()
  };

  struct masternode_registration_payload
  {
    uint8_t version = TX_EXTRA_MASTERNODE_REGISTRATION_VERSION;
    crypto::public_key operator_pubkey;
    masternode_collateral_outpoint collateral_outpoint;
    uint64_t collateral_amount = 0;
    crypto::hash service_endpoint_commitment;
    bool has_valid_from_height = false;
    uint64_t valid_from_height = 0;
    crypto::signature operator_signature;

    BEGIN_SERIALIZE_OBJECT()
      FIELD(version)
      FIELD(operator_pubkey)
      FIELD(collateral_outpoint)
      VARINT_FIELD(collateral_amount)
      FIELD(service_endpoint_commitment)
      FIELD(has_valid_from_height)
      if (has_valid_from_height)
        VARINT_FIELD(valid_from_height)
      FIELD(operator_signature)
    END_SERIALIZE()
  };

  struct tx_extra_padding
  {
    size_t size;

    // load
    template <template <bool> class Archive>
    bool member_do_serialize(Archive<false>& ar)
    {
      // size - 1 - because of variant tag
      for (size = 1; size <= TX_EXTRA_PADDING_MAX_COUNT; ++size)
      {
        if (ar.eof())
          break;

        uint8_t zero;
        if (!::do_serialize(ar, zero))
          return false;

        if (0 != zero)
          return false;
      }

      return size <= TX_EXTRA_PADDING_MAX_COUNT;
    }

    // store
    template <template <bool> class Archive>
    bool member_do_serialize(Archive<true>& ar)
    {
      if(TX_EXTRA_PADDING_MAX_COUNT < size)
        return false;

      // i = 1 - because of variant tag
      for (size_t i = 1; i < size; ++i)
      {
        uint8_t zero = 0;
        if (!::do_serialize(ar, zero))
          return false;
      }
      return true;
    }
  };

  struct tx_extra_pub_key
  {
    crypto::public_key pub_key;

    BEGIN_SERIALIZE()
      FIELD(pub_key)
    END_SERIALIZE()
  };

  struct tx_extra_nonce
  {
    std::string nonce;

    BEGIN_SERIALIZE()
      FIELD(nonce)
      if(TX_EXTRA_NONCE_MAX_COUNT < nonce.size()) return false;
    END_SERIALIZE()
  };

  struct tx_extra_merge_mining_tag
  {
    struct serialize_helper
    {
      tx_extra_merge_mining_tag& mm_tag;

      serialize_helper(tx_extra_merge_mining_tag& mm_tag_) : mm_tag(mm_tag_)
      {
      }

      BEGIN_SERIALIZE()
        VARINT_FIELD_N("depth", mm_tag.depth)
        FIELD_N("merkle_root", mm_tag.merkle_root)
      END_SERIALIZE()
    };

    size_t depth;
    crypto::hash merkle_root;

    // load
    template <template <bool> class Archive>
    bool member_do_serialize(Archive<false>& ar)
    {
      std::string field;
      if(!::do_serialize(ar, field))
        return false;

      binary_archive<false> iar{epee::strspan<std::uint8_t>(field)};
      serialize_helper helper(*this);
      return ::serialization::serialize(iar, helper);
    }

    // store
    template <template <bool> class Archive>
    bool member_do_serialize(Archive<true>& ar)
    {
      std::ostringstream oss;
      binary_archive<true> oar(oss);
      serialize_helper helper(*this);
      if(!::do_serialize(oar, helper))
        return false;

      std::string field = oss.str();
      return ::serialization::serialize(ar, field);
    }
  };

  // per-output additional tx pubkey for multi-destination transfers involving at least one subaddress
  struct tx_extra_additional_pub_keys
  {
    std::vector<crypto::public_key> data;

    BEGIN_SERIALIZE()
      FIELD(data)
    END_SERIALIZE()
  };

  struct tx_extra_mysterious_minergate
  {
    std::string data;

    BEGIN_SERIALIZE()
      FIELD(data)
    END_SERIALIZE()
  };

  struct tx_extra_masternode_registration
  {
    masternode_registration_payload registration;

    BEGIN_SERIALIZE()
      FIELD(registration)
    END_SERIALIZE()
  };

  struct tx_extra_mvm_contract
  {
    uint8_t version = TX_EXTRA_MVM_CONTRACT_VERSION;
    std::string action;
    std::string contract_id;
    std::string code_hash;
    std::string token_symbol;
    std::string token_name;
    uint64_t token_supply = 0;
    uint8_t token_decimals = 0;
    std::string token_from;
    std::string token_to;
    uint64_t token_amount = 0;
    std::string bytecode_hex;
    std::string monero_txid;
    bool has_monero_block_height = false;
    uint64_t monero_block_height = 0;

    BEGIN_SERIALIZE()
      FIELD(version)
      FIELD(action)
      FIELD(contract_id)
      FIELD(code_hash)
      FIELD(token_symbol)
      FIELD(token_name)
      FIELD(token_supply)
      FIELD(token_decimals)
      FIELD(token_from)
      FIELD(token_to)
      FIELD(token_amount)
      FIELD(bytecode_hex)
      if (version >= 2)
      {
        FIELD(monero_txid)
        FIELD(has_monero_block_height)
        if (has_monero_block_height)
          VARINT_FIELD(monero_block_height)
      }
      if (version < TX_EXTRA_MVM_CONTRACT_VERSION_MIN || version > TX_EXTRA_MVM_CONTRACT_VERSION) return false;
      if (action.empty() || action.size() > TX_EXTRA_MVM_ACTION_MAX_SIZE) return false;
      if (contract_id.empty() || contract_id.size() > TX_EXTRA_MVM_CONTRACT_ID_MAX_SIZE) return false;
      if (code_hash.empty() || code_hash.size() > TX_EXTRA_MVM_CODE_HASH_MAX_SIZE) return false;
      if (token_symbol.size() > TX_EXTRA_MVM_TOKEN_SYMBOL_MAX_SIZE) return false;
      if (token_name.size() > TX_EXTRA_MVM_TOKEN_NAME_MAX_SIZE) return false;
      if (token_decimals > 30) return false;
      if (token_supply > TX_EXTRA_MVM_TOKEN_SUPPLY_MAX) return false;
      if (token_amount > TX_EXTRA_MVM_TOKEN_AMOUNT_MAX) return false;
      if (token_from.size() > TX_EXTRA_MVM_TOKEN_ADDRESS_MAX_SIZE) return false;
      if (token_to.size() > TX_EXTRA_MVM_TOKEN_ADDRESS_MAX_SIZE) return false;
      if (bytecode_hex.size() > TX_EXTRA_MVM_BYTECODE_MAX_SIZE) return false;
      if (monero_txid.size() > TX_EXTRA_MVM_TXID_MAX_SIZE) return false;
      if (!bytecode_hex.empty())
      {
        if (bytecode_hex.size() % 2 != 0) return false;
        for (const char c : bytecode_hex)
        {
          if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        }
      }
      if (!monero_txid.empty())
      {
        if (monero_txid.size() % 2 != 0) return false;
        for (const char c : monero_txid)
        {
          if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        }
      }
      const bool known_action =
          action == "create_contract" ||
          action == "create_token" ||
          action == "mint_token" ||
          action == "transfer_token";
      if (!known_action) return false;
      if (action == "create_token")
      {
        if (token_symbol.empty() || token_supply == 0 || bytecode_hex.empty()) return false;
      }
      if (action == "transfer_token")
      {
        if (token_symbol.empty() || token_from.empty() || token_to.empty() || token_amount == 0) return false;
        if (token_from == token_to) return false;
      }
      if (action == "mint_token")
      {
        if (token_symbol.empty() || token_to.empty() || token_amount == 0) return false;
      }
      if (action == "create_contract")
      {
        if (bytecode_hex.empty()) return false;
      }
    END_SERIALIZE()
  };

  // tx_extra_field format, except tx_extra_padding and tx_extra_pub_key:
  //   varint tag;
  //   varint size;
  //   varint data[];
  typedef boost::variant<tx_extra_padding, tx_extra_pub_key, tx_extra_nonce, tx_extra_merge_mining_tag, tx_extra_additional_pub_keys, tx_extra_masternode_registration, tx_extra_mvm_contract, tx_extra_mysterious_minergate> tx_extra_field;
}

VARIANT_TAG(binary_archive, cryptonote::tx_extra_padding, TX_EXTRA_TAG_PADDING);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_pub_key, TX_EXTRA_TAG_PUBKEY);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_nonce, TX_EXTRA_NONCE);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_merge_mining_tag, TX_EXTRA_MERGE_MINING_TAG);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_additional_pub_keys, TX_EXTRA_TAG_ADDITIONAL_PUBKEYS);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_masternode_registration, TX_EXTRA_TAG_MASTERNODE_REGISTRATION);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_mvm_contract, TX_EXTRA_TAG_MVM_CONTRACT);
VARIANT_TAG(binary_archive, cryptonote::tx_extra_mysterious_minergate, TX_EXTRA_MYSTERIOUS_MINERGATE_TAG);
