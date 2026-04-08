#include "gtest/gtest.h"

#include <cstdlib>
#include <cstring>

#include "crypto/crypto.h"
#include "ringct/rctSigs.h"
#include "cryptonote_basic/account.h"
#include "fcmp_pp/curve_tree.h"
#include "fcmp_pp/rust_bridge.h"
#include "crypto/curve_switch.h"

TEST(FCMPPlus, KeyImagePedersenToggle)
{
  crypto::public_key pub;
  crypto::secret_key sec;
  crypto::generate_keys(pub, sec);

  unsetenv("MONERO_FCMPPP_KEY_IMAGE");
  crypto::key_image legacy_expected;
  crypto::generate_key_image(pub, sec, legacy_expected);
  crypto::key_image legacy;
  crypto::generate_key_image(pub, sec, legacy);
  ASSERT_EQ(legacy_expected, legacy);

  setenv("MONERO_FCMPPP_KEY_IMAGE", "1", 1);
  crypto::key_image fcmp;
  crypto::generate_key_image(pub, sec, fcmp);
  unsetenv("MONERO_FCMPPP_KEY_IMAGE");

  ASSERT_NE(legacy, fcmp);
  ASSERT_EQ(fcmp.data[31] & 0x80, 0);
}

TEST(FCMPPlus, InnerProductCompositionProofRoundTrip)
{
  rct::keyV ring;
  ring.reserve(16);
  crypto::secret_key target_secret;
  unsigned int secret_index = 7;
  for (size_t i = 0; i < 16; ++i)
  {
    crypto::public_key pub;
    crypto::secret_key sec;
    crypto::generate_keys(pub, sec);
    if (i == secret_index)
      target_secret = sec;
    ring.push_back(rct::pk2rct(pub));
  }

  rct::FCMPPlus_ResetUsedLinkingTags();
  crypto::public_key pub0, pub1;
  crypto::secret_key sec0, sec1;
  crypto::generate_keys(pub0, sec0);
  crypto::generate_keys(pub1, sec1);

  const rct::key message = rct::hash_to_scalar(ring[0]);
  const rct::fcmpplus_proof proof = rct::FCMPPlus_Gen(message, ring, rct::sk2rct(target_secret), secret_index);

  ASSERT_TRUE(rct::FCMPPlus_Ver(message, ring, proof));
  rct::keyV wrong_ring = ring;
  crypto::public_key replacement_pub;
  crypto::secret_key replacement_sec;
  crypto::generate_keys(replacement_pub, replacement_sec);
  wrong_ring[secret_index] = rct::pk2rct(replacement_pub);
  ASSERT_FALSE(rct::FCMPPlus_Ver(message, wrong_ring, proof));
}


TEST(FCMPPlus, Ring16ProofGenerationAndVerification)
{
  rct::keyV ring;
  ring.reserve(16);

  crypto::secret_key real_secret;
  constexpr unsigned int real_index = 5;
  for (size_t i = 0; i < 16; ++i)
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

TEST(FCMPPlus, ProofFailsForWrongOutput)
{
  rct::keyV ring;
  ring.reserve(16);

  crypto::secret_key real_secret;
  constexpr unsigned int real_index = 9;
  for (size_t i = 0; i < 16; ++i)
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

  rct::keyV wrong_ring = ring;
  crypto::public_key replacement_pub;
  crypto::secret_key replacement_sec;
  crypto::generate_keys(replacement_pub, replacement_sec);
  wrong_ring[real_index] = rct::pk2rct(replacement_pub);

  ASSERT_FALSE(rct::FCMPPlus_Ver(message, wrong_ring, proof));
}

TEST(FCMPPlus, ProofFailsWhenTreeRootDoesNotMatch)
{
  rct::keyV ring;
  ring.reserve(16);

  crypto::secret_key real_secret;
  constexpr unsigned int real_index = 3;
  for (size_t i = 0; i < 16; ++i)
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
  rct::fcmpplus_proof proof = rct::FCMPPlus_Gen(message, ring, rct::sk2rct(real_secret), real_index);

  proof.c0 = rct::hash_to_scalar(proof.c0);
  ASSERT_FALSE(rct::FCMPPlus_Ver(message, ring, proof));
}

TEST(FCMPPlus, RejectsSignBitSetCommitment)
{
  rct::FCMPPlus_ResetUsedLinkingTags();
  crypto::public_key pub0, pub1;
  crypto::secret_key sec0, sec1;
  crypto::generate_keys(pub0, sec0);
  crypto::generate_keys(pub1, sec1);

  rct::keyV ring{rct::pk2rct(pub0), rct::pk2rct(pub1)};
  const rct::key message = rct::hash_to_scalar(rct::pk2rct(pub0));
  rct::fcmpplus_proof proof = rct::FCMPPlus_Gen(message, ring, rct::sk2rct(sec1), 1);
  proof.key_image_commitment.bytes[31] |= 0x80;

  ASSERT_FALSE(rct::FCMPPlus_Ver(message, ring, proof));
}

TEST(FCMPPlus, LinkingTagDoubleSpendDetected)
{
  rct::FCMPPlus_ResetUsedLinkingTags();
  crypto::public_key pub0, pub1;
  crypto::secret_key sec0, sec1;
  crypto::generate_keys(pub0, sec0);
  crypto::generate_keys(pub1, sec1);

  rct::keyV ring{rct::pk2rct(pub0), rct::pk2rct(pub1)};
  const rct::key message = rct::hash_to_scalar(rct::pk2rct(pub0));
  const rct::fcmpplus_proof proof = rct::FCMPPlus_Gen(message, ring, rct::sk2rct(sec1), 1);

  ASSERT_TRUE(rct::FCMPPlus_Ver(message, ring, proof));
  ASSERT_FALSE(rct::FCMPPlus_Ver(message, ring, proof));
}

TEST(FCMPPlus, ProofFailsForWrongMessage)
{
  rct::keyV ring;
  ring.reserve(16);

  crypto::secret_key real_secret;
  constexpr unsigned int real_index = 6;
  for (size_t i = 0; i < 16; ++i)
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
  const rct::key wrong_message = rct::hash_to_scalar(ring[1]);

  ASSERT_FALSE(rct::FCMPPlus_Ver(wrong_message, ring, proof));
}

TEST(CarrotWallet, AccountKeysDefaultCompatibility)
{
  cryptonote::account_keys keys{};
  ASSERT_FALSE(keys.m_is_carrot);
  keys.m_is_carrot = true;
  ASSERT_TRUE(keys.m_is_carrot);
}

TEST(CarrotDB, SymmetricSharedSecretDerivationIsDeterministic)
{
  struct shared_secret_input
  {
    crypto::hash nonce;
    crypto::public_key view_key;
  } in{};

  in.nonce = crypto::rand<crypto::hash>();
  crypto::secret_key view_sec;
  crypto::generate_keys(in.view_key, view_sec);

  crypto::ec_scalar s1, s2;
  crypto::hash_to_scalar(&in, sizeof(in), s1);
  crypto::hash_to_scalar(&in, sizeof(in), s2);
  ASSERT_EQ(memcmp(&s1, &s2, sizeof(s1)), 0);
}

TEST(FCMPPlusCurveTree, AlternatingCurveHashingDiffers)
{
  const rct::key scalar = rct::hash_to_scalar(rct::identity());
  rct::key selene_point;
  rct::key helios_point;
  rct::fcmp_pp::scalarmultBase_curve(selene_point, scalar, rct::fcmp_pp::curve_id::SELENE);
  rct::fcmp_pp::scalarmultBase_curve(helios_point, scalar, rct::fcmp_pp::curve_id::HELIOS);

  ASSERT_FALSE(rct::equalKeys(selene_point, helios_point));
}

TEST(FCMPPlusRustBridge, GenerateAndVerifyProofRoundTrip)
{
  const rct::key message = rct::hash_to_scalar(rct::H);
  const rct::key secret = rct::hash_to_scalar(rct::G);
  rct::keyV ring{rct::G, rct::H, rct::H2};
  rct::fcmp_pp::curve_tree tree{{rct::G, rct::identity(), rct::identity()}};
  const rct::key root = rct::fcmp_pp::compute_root(tree);

  const std::vector<std::size_t> decoys{1, 2};
  const std::vector<std::uint8_t> proof = rct::fcmp_pp::fcmp_pp_generate_proof(0, root, decoys, ring, secret, message);
  ASSERT_TRUE(rct::fcmp_pp::fcmp_pp_verify_proof(proof, root, secret, ring, message));
}

TEST(FCMPPlusCurveTree, ReorgTrimRestoresOriginalRoot)
{
  rct::fcmp_pp::curve_tree tree;
  const rct::fcmp_pp::output_tuple leaf0{rct::G, rct::identity(), rct::H};
  const rct::fcmp_pp::output_tuple leaf1{rct::H, rct::identity(), rct::H2};
  const rct::fcmp_pp::output_tuple leaf2{rct::H2, rct::identity(), rct::G};

  rct::fcmp_pp::grow_tree(tree, {leaf0, leaf1});
  const rct::key root_before = rct::fcmp_pp::compute_root(tree);

  rct::fcmp_pp::grow_tree(tree, {leaf2});
  ASSERT_FALSE(rct::equalKeys(root_before, rct::fcmp_pp::compute_root(tree)));

  rct::fcmp_pp::trim_tree(tree, 1);
  const rct::key root_after_reorg = rct::fcmp_pp::compute_root(tree);
  ASSERT_TRUE(rct::equalKeys(root_before, root_after_reorg));
}
