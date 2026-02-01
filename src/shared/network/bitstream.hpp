#pragma once

#include "network_types.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

namespace network
{

class Bit_Writer
{
public:
  std::vector<uint8> buffer;
  int bit_index = 0;

  void align()
  {
    // Simple byte alignment
    if (bit_index % 8 != 0)
    {
      bit_index += (8 - (bit_index % 8));
    }
  }

  void write_bytes(const void *data, size_t size)
  {
    align();
    size_t current_byte = bit_index / 8;
    size_t needed_size = current_byte + size;

    if (buffer.size() < needed_size)
    {
      buffer.resize(needed_size);
    }

    std::memcpy(buffer.data() + current_byte, data, size);
    bit_index += size * 8;
  }

  // A real implementation would handle bit-level writes (e.g. Bool as 1 bit)
  // For this prototype, we'll just write bools as bytes for simplicity or impl
  // later. Let's implement a simple bit write for bools.

  void write_bit(bool value)
  {
    size_t byte_pos = bit_index / 8;
    size_t bit_pos = bit_index % 8;

    if (buffer.size() <= byte_pos)
    {
      buffer.push_back(0);
    }

    if (value)
    {
      buffer[byte_pos] |= (1 << bit_pos);
    }
    else
    {
      buffer[byte_pos] &= ~(1 << bit_pos);
    }
    bit_index++;
  }
};

class Bit_Reader
{
public:
  const uint8 *buffer;
  size_t size;
  int bit_index = 0;

  Bit_Reader(const uint8 *buf, size_t sz) : buffer(buf), size(sz) {}

  void align()
  {
    if (bit_index % 8 != 0)
    {
      bit_index += (8 - (bit_index % 8));
    }
  }

  void read_bytes(void *out_data, size_t count)
  {
    align();
    size_t byte_pos = bit_index / 8;
    if (byte_pos + count > size)
    {
      // Error handling needed in real code
      std::memset(out_data, 0, count);
      return;
    }
    std::memcpy(out_data, buffer + byte_pos, count);
    bit_index += count * 8;
  }

  bool read_bit()
  {
    size_t byte_pos = bit_index / 8;
    size_t bit_pos = bit_index % 8;

    if (byte_pos >= size)
      return false;

    bool val = (buffer[byte_pos] >> bit_pos) & 1;
    bit_index++;
    return val;
  }
};

} // namespace network
