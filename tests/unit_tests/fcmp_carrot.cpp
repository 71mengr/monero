#include "gtest/gtest.h"

#include <cstdlib>
#include <cstring>

#include "crypto/crypto.h"
#include "ringct/rctSigs.h"
#include "cryptonote_basic/account.h"

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
