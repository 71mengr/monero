#pragma once

#include <cstdint>

#include "cryptonote_config.h"

namespace service_subscription
{
  static constexpr uint32_t one_month = 1;
  static constexpr uint32_t two_months = 2;
  static constexpr uint32_t one_year = 12;

  static constexpr uint64_t one_month_amount_atomic = 8 * COIN;
  static constexpr uint64_t two_months_amount_atomic = 10 * COIN;
  static constexpr uint64_t one_year_amount_atomic = 48 * COIN;
  static constexpr uint64_t expected_consensus_checksum =
      (one_month_amount_atomic * 1315423911ull) ^
      (two_months_amount_atomic * 2654435761ull) ^
      (one_year_amount_atomic * 889523592379ull);

  static_assert(one_month_amount_atomic == 8 * COIN, "Consensus guard: 1-month amount changed");
  static_assert(two_months_amount_atomic == 10 * COIN, "Consensus guard: 2-month amount changed");
  static_assert(one_year_amount_atomic == 48 * COIN, "Consensus guard: 12-month amount changed");

  inline bool has_supported_duration(const uint32_t months)
  {
    return months == one_month || months == two_months || months == one_year;
  }

  inline bool is_valid_subscription_amount(const uint32_t months, const uint64_t amount_atomic)
  {
    if (amount_atomic == 0)
      return false;

    switch (months)
    {
      case one_month:
        return amount_atomic == one_month_amount_atomic;
      case two_months:
        return amount_atomic == two_months_amount_atomic;
      case one_year:
        return amount_atomic == one_year_amount_atomic;
      default:
        return false;
    }
  }

  inline uint64_t consensus_checksum()
  {
    return (one_month_amount_atomic * 1315423911ull) ^
           (two_months_amount_atomic * 2654435761ull) ^
           (one_year_amount_atomic * 889523592379ull);
  }

  inline bool is_consensus_intact()
  {
    return consensus_checksum() == expected_consensus_checksum;
  }

  inline bool masternode_guardian_consensus_ok()
  {
    return is_consensus_intact();
  }
}
