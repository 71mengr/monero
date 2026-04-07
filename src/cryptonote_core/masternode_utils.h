#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cryptonote_basic/tx_extra.h"
#include "cryptonote_basic/cryptonote_basic.h"
#include "cryptonote_protocol/cryptonote_protocol_defs.h"

namespace cryptonote
{
class BlockchainDB;

namespace masternode
{
std::string make_collateral_outpoint_key(const masternode_collateral_outpoint& collateral_outpoint);

bool validate_registration_rules_for_block(
    const std::vector<std::pair<transaction, blobdata>>& txs,
    const std::unordered_map<std::string, std::string>& by_operator_key,
    const std::unordered_map<std::string, std::string>& by_collateral_outpoint,
    BlockchainDB& db,
    network_type nettype,
    uint64_t block_height);

bool serialize_blob(const bonded_validator_info& masternode, blobdata& blob);

p2p_masternode_info make_p2p_info(const bonded_validator_info& masternode);

bool get_registration_payload_from_tx(
    const transaction& tx,
    masternode_registration_payload& registration_payload);

bool get_attestation_id_from_miner_tx_extra(const transaction& miner_tx, std::string& validator_id);

}  // namespace masternode
}  // namespace cryptonote
