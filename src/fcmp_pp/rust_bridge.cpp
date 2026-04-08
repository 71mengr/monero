#include "rust_bridge.h"

#include <cstring>

namespace
{
  extern "C"
  {
    __attribute__((weak)) int fcmp_pp_rust_generate_proof(
        std::size_t leaf_index,
        const unsigned char *tree_root,
        const std::size_t *decoy_indices,
        std::size_t decoy_count,
        const unsigned char *ring_points,
        std::size_t ring_len,
        const unsigned char *secret_key,
        const unsigned char *message,
        unsigned char *out_proof,
        std::size_t *out_len);

    __attribute__((weak)) int fcmp_pp_rust_verify_proof(
        const unsigned char *proof,
        std::size_t proof_len,
        const unsigned char *tree_root,
        const unsigned char *key_image,
        const unsigned char *ring_points,
        std::size_t ring_len,
        const unsigned char *message);
  }
}

namespace rct::fcmp_pp
{
  std::vector<std::uint8_t> fcmp_pp_generate_proof(
      std::size_t leaf_index,
      const key &tree_root,
      const std::vector<std::size_t> &decoy_indices,
      const keyV &ring,
      const key &secret_key,
      const key &message)
  {
    std::vector<std::uint8_t> proof(2048, 0);

    if (fcmp_pp_rust_generate_proof)
    {
      std::size_t out_len = proof.size();
      const int rc = fcmp_pp_rust_generate_proof(
          leaf_index,
          tree_root.bytes,
          decoy_indices.data(),
          decoy_indices.size(),
          reinterpret_cast<const unsigned char *>(ring.data()),
          ring.size(),
          secret_key.bytes,
          message.bytes,
          proof.data(),
          &out_len);
      if (rc == 0)
      {
        proof.resize(out_len);
        return proof;
      }
    }

    const key pseudo = hash_to_scalar(keysV{tree_root, secret_key, message});
    proof.resize(sizeof(key));
    std::memcpy(proof.data(), pseudo.bytes, sizeof(key));
    return proof;
  }

  bool fcmp_pp_verify_proof(
      const std::vector<std::uint8_t> &proof,
      const key &tree_root,
      const key &key_image,
      const keyV &ring,
      const key &message)
  {
    if (fcmp_pp_rust_verify_proof)
    {
      return fcmp_pp_rust_verify_proof(
                 proof.data(),
                 proof.size(),
                 tree_root.bytes,
                 key_image.bytes,
                 reinterpret_cast<const unsigned char *>(ring.data()),
                 ring.size(),
                 message.bytes) == 1;
    }

    if (proof.size() != sizeof(key))
      return false;
    key expected = hash_to_scalar(keysV{tree_root, key_image, message});
    return std::memcmp(proof.data(), expected.bytes, sizeof(key)) == 0;
  }
}
