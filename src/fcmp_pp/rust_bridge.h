#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ringct/rctOps.h"

namespace rct::fcmp_pp
{
  std::vector<std::uint8_t> fcmp_pp_generate_proof(
      std::size_t leaf_index,
      const key &tree_root,
      const std::vector<std::size_t> &decoy_indices,
      const keyV &ring,
      const key &secret_key,
      const key &message);

  bool fcmp_pp_verify_proof(
      const std::vector<std::uint8_t> &proof,
      const key &tree_root,
      const key &key_image,
      const keyV &ring,
      const key &message);
}
