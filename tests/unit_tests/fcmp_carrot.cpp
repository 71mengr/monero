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
