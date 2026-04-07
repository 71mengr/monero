#include "masternode_utils.h"

#include <limits>
#include <typeinfo>
#include <unordered_set>

#include "blockchain_db/blockchain_db.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_config.h"
#include "epee/string_tools.h"

namespace cryptonote
{
namespace
{
constexpr uint64_t MASTERNODE_COLLATERAL_EXACT_AMOUNT = 1500000000000ULL;
constexpr uint64_t MASTERNODE_MIN_COLLATERAL_LOCK_BLOCKS = (30ULL * 24ULL * 60ULL * 60ULL) / DIFFICULTY_TARGET_V2;
constexpr char MASTERNODE_REGISTRATION_SIG_DOMAIN[] = "monero-masternode-registration-v2";

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
  preimage.reserve(sizeof(MASTERNODE_REGISTRATION_SIG_DOMAIN) + sizeof(get_config(nettype).NETWORK_ID.data) + sizeof(tx_hash) + sizeof(registration.version) + payload_blob.size());
  preimage.append(MASTERNODE_REGISTRATION_SIG_DOMAIN, sizeof(MASTERNODE_REGISTRATION_SIG_DOMAIN) - 1);
  preimage.append(reinterpret_cast<const char*>(get_config(nettype).NETWORK_ID.data), sizeof(get_config(nettype).NETWORK_ID.data));
  preimage.append(reinterpret_cast<const char*>(&tx_hash), sizeof(tx_hash));
  preimage.push_back(static_cast<char>(registration.version));
  preimage.append(payload_blob);
  signature_hash = crypto::cn_fast_hash(preimage.data(), preimage.size());
  return true;
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
    const std::string collateral_outpoint = make_collateral_outpoint_key(registration.collateral_outpoint);

    if (registration.version != TX_EXTRA_MASTERNODE_REGISTRATION_VERSION)
      return false;
    if (!crypto::check_key(registration.operator_pubkey))
      return false;
    if (registration.service_endpoint_commitment == crypto::null_hash)
      return false;

    if (seen_operator_keys.count(operator_key) > 0)
      return false;
    if (seen_collateral_outpoints.count(collateral_outpoint) > 0)
      return false;

    transaction collateral_tx{};
    if (!db.get_tx(registration.collateral_outpoint.txid, collateral_tx))
      return false;
    if (registration.collateral_outpoint.vout >= collateral_tx.vout.size())
      return false;

    if (registration.collateral_amount != MASTERNODE_COLLATERAL_EXACT_AMOUNT)
      return false;
    if (collateral_tx.vout[registration.collateral_outpoint.vout].amount != registration.collateral_amount)
      return false;

    if (collateral_tx.unlock_time >= CRYPTONOTE_MAX_BLOCK_NUMBER)
      return false;
    if (block_height > std::numeric_limits<uint64_t>::max() - MASTERNODE_MIN_COLLATERAL_LOCK_BLOCKS)
      return false;
    const uint64_t min_lock_height = block_height + MASTERNODE_MIN_COLLATERAL_LOCK_BLOCKS;
    if (collateral_tx.unlock_time < min_lock_height)
      return false;

    const uint64_t collateral_height = db.get_tx_block_height(registration.collateral_outpoint.txid);
    if (collateral_height + CRYPTONOTE_DEFAULT_TX_SPENDABLE_AGE > block_height)
      return false;

    crypto::hash signing_hash{};
    if (!make_registration_signature_hash(registration, tx_hash, nettype, signing_hash))
      return false;
    if (!crypto::check_signature(signing_hash, registration.operator_pubkey, registration.operator_signature))
      return false;

    seen_operator_keys.insert(operator_key);
    seen_collateral_outpoints.insert(collateral_outpoint);
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

  if (extra_nonce.nonce.rfind("mnid:", 0) != 0)
    return false;

  validator_id = extra_nonce.nonce.substr(5);
  return !validator_id.empty();
}

}  // namespace masternode
}  // namespace cryptonote
