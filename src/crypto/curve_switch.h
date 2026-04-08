#pragma once

#include <cstdint>

#include "ringct/rctOps.h"

namespace rct::fcmp_pp
{
  enum class curve_id : uint8_t
  {
    SELENE = 0,
    HELIOS = 1
  };

  inline key curve25519_to_helios_scalar(const key &point)
  {
    return hash_to_scalar(keysV{point, H});
  }

  inline key helios_to_curve25519_scalar(const key &point)
  {
    return hash_to_scalar(keysV{point, G});
  }

  inline void scalarmultBase_curve(key &point, const key &scalar, curve_id curve)
  {
    if (curve == curve_id::SELENE)
    {
      scalarmultBase(point, scalar);
      return;
    }

    const key helios_scalar = hash_to_scalar(keysV{scalar, H2});
    scalarmultBase(point, helios_scalar);
  }
}
