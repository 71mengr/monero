#pragma once

#include <cstdint>

#include "ringct/rctTypes.h"

namespace rct::fcmp_pp
{
  enum class curve_id : uint8_t
  {
    SELENE = 0,
    HELIOS = 1
  };

  key curve25519_to_helios_scalar(const key &point);
  key helios_to_curve25519_scalar(const key &point);
}
