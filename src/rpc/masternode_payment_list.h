#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rpc/core_rpc_server_commands_defs.h"

namespace cryptonote
{
  class core;
  struct block;

  bool get_masternode_payment_id_from_block(const block& block, std::string& payment_id);

  uint64_t get_masternode_reward_from_block(const block& block);

  void collect_masternode_payment_entries(core& core,
      const std::string& masternode_id,
      uint64_t from_height,
      uint64_t to_height,
      std::vector<COMMAND_RPC_GET_BONDED_VALIDATOR_REWARDS::reward_entry>& rewards,
      uint64_t& total_amount);
}

