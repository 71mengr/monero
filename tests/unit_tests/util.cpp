// Copyright (c) 2023-2023, The Monero Project
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

#include "gtest/gtest.h"

#include "common/util.h"
#include "common/mvm.h"

TEST(Web3Id, deterministic)
{
  ASSERT_EQ(tools::make_unique_web3_id("uzoqam"), tools::make_unique_web3_id("uzoqam"));
}

TEST(Web3Id, unique_for_different_inputs)
{
  ASSERT_NE(tools::make_unique_web3_id("uzoqam"), tools::make_unique_web3_id("web3"));
}

TEST(Web3Id, prefixed_and_expected_size)
{
  const std::string web3_id = tools::make_unique_web3_id("uzoqam-web3-seed");
  ASSERT_TRUE(web3_id.rfind("0x", 0) == 0);
  ASSERT_EQ(web3_id.size(), 42u);
}

TEST(LocalAddress, localhost) { ASSERT_TRUE(tools::is_local_address("localhost")); }
TEST(LocalAddress, localhost_port) { ASSERT_TRUE(tools::is_local_address("localhost:18081")); }
TEST(LocalAddress, localhost_suffix) { ASSERT_TRUE(tools::is_local_address("test.localhost")); }
TEST(LocalAddress, loopback) { ASSERT_TRUE(tools::is_local_address("127.0.0.1")); }
TEST(LocalAddress, loopback_port) { ASSERT_TRUE(tools::is_local_address("127.0.0.1:18081")); }
TEST(LocalAddress, loopback_protocol) { ASSERT_TRUE(tools::is_local_address("http://127.0.0.1")); }
TEST(LocalAddress, loopback_hi) { ASSERT_TRUE(tools::is_local_address("127.255.255.255")); }
TEST(LocalAddress, loopback_lo) { ASSERT_TRUE(tools::is_local_address("127.0.0.0")); }
TEST(LocalAddress, loopback_ipv6) { ASSERT_TRUE(tools::is_local_address("[0:0:0:0:0:0:0:1]")); }

TEST(LocalAddress, onion) { ASSERT_FALSE(tools::is_local_address("vww6ybal4bd7szmgncyruucpgfkqahzddi37ktceo3ah7ngmcopnpyyd.onion")); }
TEST(LocalAddress, i2p) { ASSERT_FALSE(tools::is_local_address("xmrto2bturnore26xmrto2bturnore26xmrto2bturnore26xmr2.b32.i2p")); }
TEST(LocalAddress, valid_ip) { ASSERT_FALSE(tools::is_local_address("1.2.3.4")); }
TEST(LocalAddress, valid_ipv6) { ASSERT_FALSE(tools::is_local_address("[0:0:0:0:0:0:0:2]")); }
TEST(LocalAddress, valid_domain) { ASSERT_FALSE(tools::is_local_address("getmonero.org")); }
TEST(LocalAddress, local_prefix) { ASSERT_FALSE(tools::is_local_address("localhost.com")); }
TEST(LocalAddress, invalid) { ASSERT_FALSE(tools::is_local_address("test")); }
TEST(LocalAddress, empty) { ASSERT_FALSE(tools::is_local_address("")); }

TEST(MvmCommon, normalize_and_derive_ids)
{
  std::string normalized;
  ASSERT_TRUE(tools::normalize_mvm_bytecode_hex("0x6001600201", normalized));
  ASSERT_EQ(normalized, "6001600201");

  const std::string c1 = tools::derive_mvm_contract_id(normalized);
  const std::string c2 = tools::derive_mvm_contract_id(normalized);
  ASSERT_EQ(c1, c2);
  ASSERT_TRUE(c1.rfind("mvmc_", 0) == 0);
  ASSERT_EQ(tools::derive_mvm_code_hash(normalized).size(), 42u);
}

TEST(MvmCommon, validate_p2p_payload)
{
  ASSERT_TRUE(tools::validate_mvm_p2p_payload("create_contract", 1, 0, 0, "", ""));
  ASSERT_FALSE(tools::validate_mvm_p2p_payload("create_contract", 0, 0, 0, "", ""));
  ASSERT_FALSE(tools::validate_mvm_p2p_payload("transfer_token", 1, 0, tools::MVM_TOKEN_AMOUNT_MAX + 1, "a", "b"));
  ASSERT_FALSE(tools::validate_mvm_p2p_payload("transfer_token", 1, 0, 1, "same", "same"));
}

TEST(MvmCommon, validate_bytecode_and_mainnet_flag)
{
  ASSERT_TRUE(tools::validate_mvm_bytecode_hex_program("010200000000000000ff")); // PUSH 2; HALT
  ASSERT_FALSE(tools::validate_mvm_bytecode_hex_program("0102ff"));              // truncated PUSH
  ASSERT_TRUE(tools::is_mvm_mainnet_enabled(cryptonote::MAINNET));
  ASSERT_FALSE(tools::is_mvm_mainnet_enabled(cryptonote::TESTNET));
}
