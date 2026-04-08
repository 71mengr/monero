#pragma once

#include <cstddef>
#include <vector>

#include "ringct/rctOps.h"
#include "crypto/curve_switch.h"

namespace rct::fcmp_pp
{
  struct output_tuple
  {
    key O;
    key I;
    key C;
  };

  using curve_tree = std::vector<output_tuple>;

  void insert_leaf(curve_tree &tree, const output_tuple &leaf);
  void grow_tree(curve_tree &tree, const std::vector<output_tuple> &new_leaves);
  void trim_tree(curve_tree &tree, std::size_t leaves_to_remove);

  keyV hash_leaves_to_layer0(const curve_tree &tree, std::size_t chunk_size = 2);
  keyV hash_layer0_to_layer1(const keyV &layer0);
  key compute_root(const curve_tree &tree);

  std::vector<key> merkle_path(const curve_tree &tree, std::size_t leaf_index);
}
