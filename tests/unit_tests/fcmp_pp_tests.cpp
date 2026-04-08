#include "gtest/gtest.h"

#include "crypto/crypto.h"
#include "cryptonote_config.h"
#include "fcmp_pp/curve_trees.h"
#include "ringct/rctSigs.h"

TEST(FCMPPlusPlus, TreeRootAndMerklePath)
{
  const rct::fcmp_pp::output_tuple leaf0{rct::G, rct::identity(), rct::H};
  const rct::fcmp_pp::output_tuple leaf1{rct::H, rct::identity(), rct::H2};
  const rct::fcmp_pp::output_tuple leaf2{rct::H2, rct::identity(), rct::G};

  rct::fcmp_pp::curve_tree tree;
  rct::fcmp_pp::grow_tree(tree, {leaf0, leaf1, leaf2});

  const rct::key root_before = rct::fcmp_pp::compute_root(tree);
  const std::vector<rct::key> path = rct::fcmp_pp::merkle_path(tree, 1);
  ASSERT_FALSE(path.empty());

  rct::fcmp_pp::trim_tree(tree, 1);
  const rct::key root_after = rct::fcmp_pp::compute_root(tree);
  ASSERT_FALSE(rct::equalKeys(root_before, root_after));
}

TEST(FCMPPlusPlus, ProofRoundTrip)
{
  rct::keyV ring;
  ring.reserve(8);

  crypto::secret_key real_secret;
  constexpr unsigned int real_index = 3;
  for (size_t i = 0; i < 8; ++i)
  {
    crypto::public_key pub;
    crypto::secret_key sec;
    crypto::generate_keys(pub, sec);
    if (i == real_index)
      real_secret = sec;
    ring.push_back(rct::pk2rct(pub));
  }

  rct::FCMPPlus_ResetUsedLinkingTags();
  const rct::key message = rct::hash_to_scalar(ring[0]);
  const rct::fcmpplus_proof proof = rct::FCMPPlus_Gen(message, ring, rct::sk2rct(real_secret), real_index);

  ASSERT_TRUE(rct::FCMPPlus_Ver(message, ring, proof));
}

TEST(FCMPPlusPlus, ForkHeightGating)
{
  ASSERT_FALSE(use_fcmpp(HF_VERSION_FCMPPP - 1));
  ASSERT_TRUE(use_fcmpp(HF_VERSION_FCMPPP));
}
