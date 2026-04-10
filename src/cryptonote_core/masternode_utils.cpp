
// Copyright (c) 2014-2025, The Monero Project
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

#include "masternode_utils.h"

#include <limits>
#include <typeinfo>
#include <unordered_set>

#include "blockchain_db/blockchain_db.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_config.h"
#include "string_tools.h"

namespace cryptonote
{
namespace
{
constexpr uint64_t MASTERNODE_COLLATERAL_EXACT_AMOUNT = 1500000000000ULL;
constexpr uint64_t MASTERNODE_MIN_COLLATERAL_LOCK_BLOCKS = (30ULL * 24ULL * 60ULL * 60ULL) / DIFFICULTY_TARGET_V2;
constexpr char MASTERNODE_REGISTRATION_SIG_DOMAIN[] = "monero-masternode-registration-v2";

// Helper to safely add with overflow check
template<typename T>
bool add_overflow(T a, T b, T& out)
{
    if (std::numeric_limits<T>::max() - a < b)
        return true;
    out = a + b;
    return false;
}

bool make_registration_signature_hash(
    const masternode_registration_payload& registration,
    const crypto::hash& tx_hash,
    const network_type nettype,
    crypto::hash& signature_hash)
{
    masternode_registration_payload unsigned_payload = registration;
    unsigned_payload.operator_signature = crypto::signature{};

    blobdata payload_blob;
    if (!t_serializable_object_to_blob(unsigned_payload, payload_blob))
        return false;

    std::string preimage;
    const size_t domain_len = std::strlen(MASTERNODE_REGISTRATION_SIG_DOMAIN);
    const auto& net_id = get_config(nettype).NETWORK_ID;
    preimage.reserve(domain_len + net_id.size() + sizeof(tx_hash) + 1 + payload_blob.size());
    preimage.append(MASTERNODE_REGISTRATION_SIG_DOMAIN, domain_len);
    preimage.append(reinterpret_cast<const char*>(net_id.data), net_id.size());
    preimage.append(reinterpret_cast<const char*>(&tx_hash), sizeof(tx_hash));
    preimage.push_back(static_cast<char>(registration.version));
    preimage.append(payload_blob);
    signature_hash = crypto::cn_fast_hash(preimage.data(), preimage.size());
    return true;
}

// Check if a tx output is a standard txout_to_key
bool is_txout_to_key(const tx_out& out)
{
    return out.target.type() == typeid(txout_to_key);
}

}  // namespace

namespace masternode
{
std::string make_collateral_outpoint_key(const masternode_collateral_outpoint& collateral_outpoint)
{
    return epee::string_tools::pod_to_hex(collateral_outpoint.txid) + ":" + std::to_string(collateral_outpoint.vout);
}

bool validate_registration_rules_for_block(
    const std::vector<std::pair<transaction, blobdata>>& txs,
    const std::unordered_map<std::string, std::string>& by_operator_key,
    const std::unordered_map<std::string, std::string>& by_collateral_outpoint,
    BlockchainDB& db,
    const network_type nettype,
    const uint64_t block_height)
{
    // Track duplicates within this block
    std::unordered_set<std::string> seen_operator_keys;
    for (const auto& kv : by_operator_key)
        seen_operator_keys.insert(kv.first);
    std::unordered_set<std::string> seen_collateral_outpoints;
    for (const auto& kv : by_collateral_outpoint)
        seen_collateral_outpoints.insert(kv.first);

    for (const auto& tx_entry : txs)
    {
        const auto& tx = tx_entry.first;
        std::vector<tx_extra_field> tx_extra_fields;
        if (!parse_tx_extra(tx.extra, tx_extra_fields))
            return false;

        size_t registration_count = 0;
        masternode_registration_payload registration{};
        for (const auto& field : tx_extra_fields)
        {
            if (field.type() != typeid(tx_extra_masternode_registration))
                continue;
            ++registration_count;
            registration = boost::get<tx_extra_masternode_registration>(field).registration;
        }

        if (registration_count == 0)
            continue;
        if (registration_count > 1 || !check_masternode_registration_payload(registration))
            return false;

        const crypto::hash tx_hash = get_transaction_hash(tx);
        const std::string operator_key = epee::string_tools::pod_to_hex(registration.operator_pubkey);
        const std::string collateral_outpoint_key = make_collateral_outpoint_key(registration.collateral_outpoint);

        // Basic format checks
        if (registration.version != TX_EXTRA_MASTERNODE_REGISTRATION_VERSION)
            return false;
        if (!crypto::check_key(registration.operator_pubkey))
            return false;
        if (registration.service_endpoint_commitment == crypto::null_hash)
            return false;

        // Duplicate within block?
        if (seen_operator_keys.count(operator_key) > 0)
            return false;
        if (seen_collateral_outpoints.count(collateral_outpoint_key) > 0)
            return false;

        // Retrieve collateral transaction
        transaction collateral_tx{};
        if (!db.get_tx(registration.collateral_outpoint.txid, collateral_tx))
            return false;
        if (registration.collateral_outpoint.vout >= collateral_tx.vout.size())
            return false;

        const auto& out = collateral_tx.vout[registration.collateral_outpoint.vout];
        // Validate output type (must be spendable key)
        if (!is_txout_to_key(out))
            return false;
        if (registration.collateral_amount != MASTERNODE_COLLATERAL_EXACT_AMOUNT)
            return false;
        if (out.amount != registration.collateral_amount)
            return false;

        // Check that the output is still unspent
        if (db.is_output_spent(registration.collateral_outpoint.txid, registration.collateral_outpoint.vout))
            return false;

        // Unlock time validation – supports both block height and timestamp locks
        uint64_t current_unlock_time;
        if (collateral_tx.unlock_time < CRYPTONOTE_MAX_BLOCK_NUMBER)
        {
            // Block height lock
            current_unlock_time = collateral_tx.unlock_time;
        }
        else
        {
            // Timestamp lock: convert to equivalent block height using median timestamp
            // (We assume the DB can give median timestamp for a given height)
            uint64_t median_ts = db.get_median_timestamp_for_height(block_height);
            if (median_ts == 0)
                return false;
            // Approximate: each block takes DIFFICULTY_TARGET_V2 seconds
            uint64_t lock_height = (collateral_tx.unlock_time - median_ts) / DIFFICULTY_TARGET_V2;
            if (lock_height > block_height)
                current_unlock_time = lock_height;
            else
                current_unlock_time = block_height; // already unlocked
        }

        // Enforce minimum lock period
        uint64_t min_lock_height;
        if (add_overflow(block_height, MASTERNODE_MIN_COLLATERAL_LOCK_BLOCKS, min_lock_height))
            return false;
        if (current_unlock_time < min_lock_height)
            return false;

        // Maturity: collateral must be at least CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE blocks old
        const uint64_t collateral_height = db.get_tx_block_height(registration.collateral_outpoint.txid);
        if (collateral_height + CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE > block_height)
            return false;

        // Verify operator signature
        crypto::hash signing_hash{};
        if (!make_registration_signature_hash(registration, tx_hash, nettype, signing_hash))
            return false;
        if (!crypto::check_signature(signing_hash, registration.operator_pubkey, registration.operator_signature))
            return false;

        // All good – record to prevent duplicates in same block
        seen_operator_keys.insert(operator_key);
        seen_collateral_outpoints.insert(collateral_outpoint_key);
    }

    return true;
}

bool serialize_blob(const bonded_validator_info& masternode, blobdata& blob)
{
    return t_serializable_object_to_blob(masternode, blob);
}

p2p_masternode_info make_p2p_info(const bonded_validator_info& masternode)
{
    p2p_masternode_info result{};
    result.id = masternode.id;
    result.operator_key = masternode.operator_key;
    result.collateral_txid = masternode.collateral_txid;
    result.collateral_amount = masternode.collateral_amount;
    result.registration_height = masternode.registration_height;
    result.lock_end_height = masternode.lock_end_height;
    result.last_uptime_proof_height = masternode.last_uptime_proof_height;
    result.missed_duties = masternode.missed_duties;
    result.active = masternode.active;
    result.online = masternode.online;
    result.penalty_points = masternode.penalty_points;
    result.deregistered = masternode.deregistered;
    result.created_height = masternode.created_height;
    result.updated_height = masternode.updated_height;
    result.created_timestamp = masternode.created_timestamp;
    result.updated_timestamp = masternode.updated_timestamp;
    return result;
}

bool get_registration_payload_from_tx(
    const transaction& tx,
    masternode_registration_payload& registration_payload)
{
    return get_masternode_registration_from_tx_extra(tx.extra, registration_payload);
}

bool get_attestation_id_from_miner_tx_extra(const transaction& miner_tx, std::string& validator_id)
{
    validator_id.clear();
    std::vector<tx_extra_field> tx_extra_fields;
    if (!parse_tx_extra(miner_tx.extra, tx_extra_fields))
        return false;

    tx_extra_nonce extra_nonce;
    if (!find_tx_extra_field_by_type(tx_extra_fields, extra_nonce))
        return false;

    const std::string prefix = "mnid:";
    if (extra_nonce.nonce.rfind(prefix, 0) != 0)
        return false;

    validator_id = extra_nonce.nonce.substr(prefix.size());
    return !validator_id.empty();  // reject empty ID
}

}  // namespace masternode
}  // namespace cryptonote
