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

#include <gtest/gtest.h>

#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/tx_extra.h"
#include "serialization/binary_utils.h"

namespace
{
cryptonote::masternode_registration_payload make_registration_payload()
{
  crypto::public_key operator_pubkey{};
  crypto::secret_key operator_secret_key{};
  crypto::generate_keys(operator_pubkey, operator_secret_key);

  cryptonote::masternode_registration_payload payload{};
  payload.version = cryptonote::TX_EXTRA_MASTERNODE_REGISTRATION_VERSION;
  payload.operator_pubkey = operator_pubkey;
  payload.collateral_outpoint.txid = crypto::rand<crypto::hash>();
  payload.collateral_outpoint.vout = 5;
  payload.collateral_amount = 2500000000000;
  payload.service_endpoint_commitment = crypto::rand<crypto::hash>();
  payload.has_valid_from_height = true;
  payload.valid_from_height = 144;

  crypto::hash preimage_hash{};
  EXPECT_TRUE(cryptonote::get_masternode_registration_hash_preimage(payload, preimage_hash));
  crypto::generate_signature(preimage_hash, operator_pubkey, operator_secret_key, payload.operator_signature);

  return payload;
}
}

TEST(masternode_registration_hex, add_and_decode_payload_string_roundtrip)
{
  const auto payload = make_registration_payload();

  cryptonote::blobdata payload_blob;
  ASSERT_TRUE(t_serializable_object_to_blob(payload, payload_blob));

  std::vector<uint8_t> extra{};
  ASSERT_TRUE(cryptonote::add_masternode_registration_to_tx_extra(extra, epee::string_tools::buff_to_hex_nodelimer(payload_blob)));

  std::string registration_hex;
  ASSERT_TRUE(cryptonote::get_masternode_registration_from_tx_extra(extra, registration_hex));
  ASSERT_EQ(registration_hex, epee::string_tools::buff_to_hex_nodelimer(payload_blob));
}

TEST(masternode_registration_hex, rejects_non_hex_payload_string)
{
  std::vector<uint8_t> extra{};
  ASSERT_FALSE(cryptonote::add_masternode_registration_to_tx_extra(extra, "zzzz"));
}
