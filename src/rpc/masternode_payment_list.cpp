#include "masternode_payment_list.h"

#include "string_tools.h"
#include "cryptonote_core/cryptonote_core.h"
#include "cryptonote_basic/cryptonote_format_utils.h"

namespace cryptonote
{
  bool get_masternode_payment_id_from_block(const block& block, std::string& payment_id)
  {
    std::vector<tx_extra_field> tx_extra_fields;
    if (!parse_tx_extra(block.miner_tx.extra, tx_extra_fields))
      return false;

    tx_extra_nonce extra_nonce;
    if (!find_tx_extra_field_by_type(tx_extra_fields, extra_nonce) ||
        extra_nonce.nonce.rfind("mn:", 0) != 0)
    {
      return false;
    }

    payment_id = extra_nonce.nonce.substr(3);
    return true;
  }

  uint64_t get_masternode_reward_from_block(const block& block)
  {
    uint64_t block_reward = 0;
    for (const auto& out : block.miner_tx.vout)
      block_reward += out.amount;

    uint64_t miner_reward = 0;
    uint64_t masternode_reward = 0;
    split_reward_for_masternode(block_reward, block.major_version, miner_reward, masternode_reward);
    return masternode_reward;
  }

  void collect_masternode_payment_entries(core& core,
      const std::string& masternode_id,
      uint64_t from_height,
      uint64_t to_height,
      std::vector<COMMAND_RPC_GET_BONDED_VALIDATOR_REWARDS::reward_entry>& rewards,
      uint64_t& total_amount)
  {
    for (uint64_t height = from_height; height <= to_height; ++height)
    {
      const block block = core.get_blockchain_storage().get_db().get_block_from_height(height);
      std::string payment_id;
      if (!get_masternode_payment_id_from_block(block, payment_id) || payment_id != masternode_id)
        continue;

      const uint64_t masternode_reward = get_masternode_reward_from_block(block);
      if (masternode_reward == 0)
        continue;

      COMMAND_RPC_GET_BONDED_VALIDATOR_REWARDS::reward_entry entry{};
      entry.height = height;
      entry.amount = masternode_reward;
      entry.txid = epee::string_tools::pod_to_hex(get_transaction_hash(block.miner_tx));
      rewards.push_back(std::move(entry));
      total_amount += masternode_reward;
    }
  }
}

