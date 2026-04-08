#include "curve_tree.h"

#include <algorithm>

#include "common/expect.h"

namespace rct::fcmp_pp
{
  namespace
  {
    key point_to_curve_point(const key &point, const curve_id next_curve)
    {
      if (next_curve != curve_id::HELIOS)
        return point;

      const key helios_scalar = curve25519_to_helios_scalar(point);
      key helios_point;
      scalarmultBase_curve(helios_point, helios_scalar, curve_id::HELIOS);
      return helios_point;
    }

    key hash_leaf(const output_tuple &leaf)
    {
      return hash_to_ec_curve(keysV{leaf.O, leaf.I, leaf.C}, curve_id::SELENE);
    }

    key hash_parent(const key &left, const key &right, const std::size_t layer_depth)
    {
      const curve_id curve = (layer_depth % 2 == 0) ? curve_id::SELENE : curve_id::HELIOS;
      return hash_to_ec_curve(keysV{left, right}, curve);
    }

    keyV hash_adjacent_pairs(const keyV &layer, const std::size_t layer_depth)
    {
      if (layer.empty())
        return {};

      keyV padded = layer;
      if (padded.size() & 1)
        padded.push_back(padded.back());

      keyV next;
      next.reserve(padded.size() / 2);
      for (std::size_t i = 0; i < padded.size(); i += 2)
        next.push_back(hash_parent(padded[i], padded[i + 1], layer_depth));

      if ((layer_depth % 2) == 0)
      {
        for (key &node : next)
          node = point_to_curve_point(node, curve_id::HELIOS);
      }

      return next;
    }


    key reconstruct_root_from_path(const output_tuple &leaf, std::size_t leaf_index, const std::vector<key> &path)
    {
      key node = hash_leaf(leaf);
      std::size_t idx = leaf_index;
      for (std::size_t depth = 0; depth < path.size(); ++depth)
      {
        const key &sibling = path[depth];
        const key left = (idx % 2 == 0) ? node : sibling;
        const key right = (idx % 2 == 0) ? sibling : node;
        node = hash_parent(left, right, depth);
        if ((depth % 2) == 0)
          node = point_to_curve_point(node, curve_id::HELIOS);
        idx >>= 1;
      }
      return node;
    }
    key compute_root_impl(const curve_tree &tree)
    {
      if (tree.empty())
        return identity();

      keyV current;
      current.reserve(tree.size());
      for (const output_tuple &leaf : tree)
        current.push_back(hash_leaf(leaf));

      std::size_t layer_depth = 0;
      while (current.size() > 1)
        current = hash_adjacent_pairs(current, layer_depth++);

      return current.front();
    }
  }

  void insert_leaf(curve_tree &tree, const output_tuple &leaf)
  {
    tree.push_back(leaf);
  }

  void grow_tree(curve_tree &tree, const std::vector<output_tuple> &new_leaves)
  {
    tree.insert(tree.end(), new_leaves.begin(), new_leaves.end());
  }

  void trim_tree(curve_tree &tree, std::size_t leaves_to_remove)
  {
    if (leaves_to_remove >= tree.size())
    {
      tree.clear();
      return;
    }

    tree.resize(tree.size() - leaves_to_remove);
  }

  keyV hash_leaves_to_layer0(const curve_tree &tree, std::size_t chunk_size)
  {
    CHECK_AND_ASSERT_THROW_MES(chunk_size > 0, "curve-tree chunk size must be non-zero");

    if (tree.empty())
      return {};

    keyV layer0;
    layer0.reserve((tree.size() + chunk_size - 1) / chunk_size);

    for (std::size_t i = 0; i < tree.size(); i += chunk_size)
    {
      keysV chunk;
      chunk.reserve(chunk_size * 3);

      const std::size_t end = std::min(tree.size(), i + chunk_size);
      for (std::size_t j = i; j < end; ++j)
      {
        chunk.push_back(tree[j].O);
        chunk.push_back(tree[j].I);
        chunk.push_back(tree[j].C);
      }

      layer0.push_back(hash_to_ec_curve(chunk, curve_id::SELENE));
    }

    return layer0;
  }

  keyV hash_layer0_to_layer1(const keyV &layer0)
  {
    if (layer0.empty())
      return {};

    keyV scalar_points;
    scalar_points.reserve(layer0.size());
    for (const key &p : layer0)
      scalar_points.push_back(hash_to_ec_curve(keysV{p}, curve_id::SELENE));

    return hash_adjacent_pairs(scalar_points, 1);
  }

  key compute_root(const curve_tree &tree)
  {
    const key root = compute_root_impl(tree);

    if (!tree.empty())
    {
      const std::vector<key> path = merkle_path(tree, 0);
      const key reconstructed = reconstruct_root_from_path(tree.front(), 0, path);
      CHECK_AND_ASSERT_THROW_MES(equalKeys(root, reconstructed), "curve-tree root alternating-curve sanity check failed");
    }

    return root;
  }

  std::vector<key> merkle_path(const curve_tree &tree, std::size_t leaf_index)
  {
    CHECK_AND_ASSERT_THROW_MES(!tree.empty(), "curve-tree is empty");
    CHECK_AND_ASSERT_THROW_MES(leaf_index < tree.size(), "leaf index out of range");

    keyV current;
    current.reserve(tree.size());
    for (const output_tuple &leaf : tree)
      current.push_back(hash_leaf(leaf));

    std::size_t idx = leaf_index;
    std::vector<key> path;
    std::size_t layer_depth = 0;
    key reconstructed = current[idx];

    while (current.size() > 1)
    {
      if (current.size() & 1)
        current.push_back(current.back());

      const std::size_t sibling = idx ^ 1;
      path.push_back(current[sibling]);

      const key left = (idx % 2 == 0) ? reconstructed : current[sibling];
      const key right = (idx % 2 == 0) ? current[sibling] : reconstructed;
      reconstructed = hash_parent(left, right, layer_depth);
      if ((layer_depth % 2) == 0)
        reconstructed = point_to_curve_point(reconstructed, curve_id::HELIOS);

      current = hash_adjacent_pairs(current, layer_depth++);
      idx >>= 1;
    }

    const key expected_root = compute_root_impl(tree);
    CHECK_AND_ASSERT_THROW_MES(equalKeys(reconstructed, expected_root), "merkle path alternating-curve sanity check failed");

    return path;
  }
}
