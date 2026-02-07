#pragma once

#include "../entity.hpp"
#include "bitstream.hpp"
#include "packet.hpp"
#include "quantization.hpp"
#include "schema.hpp"

namespace network
{

template <typename T>
void pack_entity_delta_for_update(game::S2C_EntityPackage &out_packet,
                                  const T &entity, const T *baseline = nullptr)
{
  static_assert(std::is_base_of<Entity, T>::value,
                "T must be derived from Entity");

  // Always true as requested
  out_packet.set_is_delta(true);

  Bit_Writer writer;
  entity.serialize(writer, baseline);

  // set_entity_data takes a std::string or char* buffer
  out_packet.set_entity_data(writer.buffer.data(), writer.buffer.size());
}

} // namespace network
