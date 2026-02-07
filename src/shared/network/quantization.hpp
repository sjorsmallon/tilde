#pragma once

#include "bitstream.hpp"
#include <cmath>
#include <cstdlib>

namespace network
{

inline void write_var_uint(Bit_Writer &w, uint32_t value)
{
  do
  {
    uint32_t chunk = value & 0b1111; // lowest 4 bits
    value >>= 4;

    bool hasMore = (value != 0);

    w.write_bit(hasMore);   // continuation bit
    w.write_bits(chunk, 4); // payload
  } while (value != 0);
}

inline uint32_t read_var_uint(Bit_Reader &r)
{
  uint32_t value = 0;
  uint32_t shift = 0;

  while (true)
  {
    bool hasMore = r.read_bit();
    uint32_t chunk = r.read_bits(4);

    value |= (chunk << shift);

    if (!hasMore)
      break;

    shift += 4;
  }

  return value;
}

inline void write_var_int(Bit_Writer &w, int32_t value)
{
  bool negative = (value < 0);
  uint32_t magnitude = std::abs(value);

  w.write_bit(negative);
  write_var_uint(w, magnitude);
}

inline int32_t read_var_int(Bit_Reader &r)
{
  bool negative = r.read_bit();
  uint32_t magnitude = read_var_uint(r);

  return negative ? -int32_t(magnitude) : int32_t(magnitude);
}

inline void write_coord(Bit_Writer &w, float value)
{
  if (value == 0.0f)
  {
    w.write_bit(0); // has_value
    return;
  }

  w.write_bit(1); // has_value

  float absValue = std::abs(value);

  // integer part
  uint32_t integer = (uint32_t)std::floor(absValue);
  // fractional part (around 5 bits precision)
  uint32_t fraction = (uint32_t)std::round((absValue - integer) * 32.0f);

  bool hasInt = (integer != 0);
  bool hasFrac = (fraction != 0);

  w.write_bit(hasInt);
  w.write_bit(hasFrac);

  if (hasInt)
  {
    w.write_bit(value < 0); // sign
    write_var_uint(w, integer);
  }

  if (hasFrac)
  {

    if (!hasInt && hasFrac)
    {
      w.write_bit(value < 0);
    }

    w.write_bits(fraction, 5);
  }
}

inline float read_coord(Bit_Reader &r)
{
  if (!r.read_bit())
  {
    return 0.0f;
  }

  bool hasInt = r.read_bit();
  bool hasFrac = r.read_bit();

  float value = 0.0f;

  if (hasInt)
  {
    bool negative = r.read_bit();
    uint32_t integer = read_var_uint(r);
    value = float(integer);
    if (negative)
      value = -value;
  }

  if (hasFrac)
  {
    // My fix: read sign if no int
    if (!hasInt)
    {
      bool negative = r.read_bit();
      if (negative)
        value =
            -0.0f; // Mark as negative zero conceptually to affect next step?
      // Actually float -0.0f exists.
      // But we add fraction.
      // Let's store sign state.
      if (negative)
      {
        uint32_t fraction = r.read_bits(5);
        value = -(fraction / 32.0f);
        return value;
      }
    }

    uint32_t fraction = r.read_bits(5);

    if (hasInt)
    {
      value += (value < 0 ? -1.0f : 1.0f) * (fraction / 32.0f);
    }
    else
    {
      value += (fraction / 32.0f);
    }
  }

  return value;
}

} // namespace network
