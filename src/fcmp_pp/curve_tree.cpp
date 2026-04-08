#include "curve_tree.h"

#include <algorithm>
#include "common/expect.h"

namespace rct::fcmp_pp
{
  namespace
  {
    key hash_leaf(const output_tuple &leaf)
    {
      const key s = hash_to_scalar(keysV{leaf.O, leaf.I, leaf.C});
      key p;
      scalarmultBase(p, s);
      return p;
    }

    key hash_parent(const key &left, const key &right)
    {
      const key s = hash_to_scalar(keysV{left, right});
      key parent;
      scalarmultBase(parent, s);
      return parent;
    }

    keyV hash_adjacent_pairs(const keyV &layer)
    {
      if (layer.empty())
        return {};

      keyV padded = layer;
      if (padded.size() & 1)
        padded.push_back(padded.back());

      keyV next;
      next.reserve(padded.size() / 2);
      for (std::size_t i = 0; i < padded.size(); i += 2)
        next.push_back(hash_parent(padded[i], padded[i + 1]));
      return next;
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

      const key s = hash_to_scalar(chunk);
      key p;
      scalarmultBase(p, s);
      layer0.push_back(p);
    }

    return layer0;
  }

  keyV hash_layer0_to_layer1(const keyV &layer0)
  {
    if (layer0.empty())
      return {};

    keysV scalars;
    scalars.reserve(layer0.size());
    for (const key &p : layer0)
      scalars.push_back(hash_to_scalar(p));

    keyV scalar_points;
    scalar_points.reserve(scalars.size());
    for (const key &s : scalars)
    {
      key p;
      scalarmultBase(p, s);
      scalar_points.push_back(p);
    }

    return hash_adjacent_pairs(scalar_points);
  }

  key compute_root(const curve_tree &tree)
  {
    if (tree.empty())
      return identity();

    keyV current;
    current.reserve(tree.size());
    for (const output_tuple &leaf : tree)
      current.push_back(hash_leaf(leaf));

    while (current.size() > 1)
      current = hash_adjacent_pairs(current);

    return current.front();
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

    while (current.size() > 1)
    {
      if (current.size() & 1)
        current.push_back(current.back());

      const std::size_t sibling = idx ^ 1;
      path.push_back(current[sibling]);

      keyV next;
      next.reserve(current.size() / 2);
      for (std::size_t i = 0; i < current.size(); i += 2)
        next.push_back(hash_parent(current[i], current[i + 1]));

      current = std::move(next);
      idx >>= 1;
    }

    return path;
  }
}
