// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/effects/effects.def by def_gen. Do not edit.
#include "effects/generated/effects_generated.hpp"

#include "log.hpp"
#include "network/field_codec.hpp"

#include <cassert>
#include <cstddef>
#include <format>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#elif defined(_MSC_VER)
#pragma warning(disable : 4841)
#endif

namespace shared
{

namespace
{

constexpr field_info_t ROCKET_EXPLOSION_FIELDS[] = {
  {.name = "origin",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Rocket_Explosion, origin),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Explosion::origin),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "normal",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Rocket_Explosion, normal),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Explosion::normal),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Rocket_Explosion, color),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Explosion::color),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "scale",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Explosion, scale),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Explosion::scale),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "attached_entity",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Rocket_Explosion, attached_entity),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Explosion::attached_entity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "surface_material",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Rocket_Explosion, surface_material),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Explosion::surface_material),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t BULLET_IMPACT_FIELDS[] = {
  {.name = "origin",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Bullet_Impact, origin),
   .size_in_bytes = (uint32_t)sizeof(Bullet_Impact::origin),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "normal",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Bullet_Impact, normal),
   .size_in_bytes = (uint32_t)sizeof(Bullet_Impact::normal),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Bullet_Impact, color),
   .size_in_bytes = (uint32_t)sizeof(Bullet_Impact::color),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "scale",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Bullet_Impact, scale),
   .size_in_bytes = (uint32_t)sizeof(Bullet_Impact::scale),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "attached_entity",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Bullet_Impact, attached_entity),
   .size_in_bytes = (uint32_t)sizeof(Bullet_Impact::attached_entity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "surface_material",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Bullet_Impact, surface_material),
   .size_in_bytes = (uint32_t)sizeof(Bullet_Impact::surface_material),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t FOOTSTEP_FIELDS[] = {
  {.name = "origin",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Footstep, origin),
   .size_in_bytes = (uint32_t)sizeof(Footstep::origin),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "normal",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Footstep, normal),
   .size_in_bytes = (uint32_t)sizeof(Footstep::normal),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Footstep, color),
   .size_in_bytes = (uint32_t)sizeof(Footstep::color),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "scale",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Footstep, scale),
   .size_in_bytes = (uint32_t)sizeof(Footstep::scale),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "attached_entity",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Footstep, attached_entity),
   .size_in_bytes = (uint32_t)sizeof(Footstep::attached_entity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "surface_material",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Footstep, surface_material),
   .size_in_bytes = (uint32_t)sizeof(Footstep::surface_material),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t JUMP_FIELDS[] = {
  {.name = "origin",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Jump, origin),
   .size_in_bytes = (uint32_t)sizeof(Jump::origin),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "normal",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Jump, normal),
   .size_in_bytes = (uint32_t)sizeof(Jump::normal),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Jump, color),
   .size_in_bytes = (uint32_t)sizeof(Jump::color),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "scale",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Jump, scale),
   .size_in_bytes = (uint32_t)sizeof(Jump::scale),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "attached_entity",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Jump, attached_entity),
   .size_in_bytes = (uint32_t)sizeof(Jump::attached_entity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "surface_material",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Jump, surface_material),
   .size_in_bytes = (uint32_t)sizeof(Jump::surface_material),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t LAND_FIELDS[] = {
  {.name = "origin",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Land, origin),
   .size_in_bytes = (uint32_t)sizeof(Land::origin),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "normal",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Land, normal),
   .size_in_bytes = (uint32_t)sizeof(Land::normal),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Land, color),
   .size_in_bytes = (uint32_t)sizeof(Land::color),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "scale",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Land, scale),
   .size_in_bytes = (uint32_t)sizeof(Land::scale),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "attached_entity",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Land, attached_entity),
   .size_in_bytes = (uint32_t)sizeof(Land::attached_entity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "surface_material",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Land, surface_material),
   .size_in_bytes = (uint32_t)sizeof(Land::surface_material),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t FLESH_IMPACT_FIELDS[] = {
  {.name = "origin",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Flesh_Impact, origin),
   .size_in_bytes = (uint32_t)sizeof(Flesh_Impact::origin),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "normal",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Flesh_Impact, normal),
   .size_in_bytes = (uint32_t)sizeof(Flesh_Impact::normal),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Flesh_Impact, color),
   .size_in_bytes = (uint32_t)sizeof(Flesh_Impact::color),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "scale",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Flesh_Impact, scale),
   .size_in_bytes = (uint32_t)sizeof(Flesh_Impact::scale),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "attached_entity",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Flesh_Impact, attached_entity),
   .size_in_bytes = (uint32_t)sizeof(Flesh_Impact::attached_entity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "surface_material",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Flesh_Impact, surface_material),
   .size_in_bytes = (uint32_t)sizeof(Flesh_Impact::surface_material),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

} // namespace

const char* to_string(effect_type value)
{
  switch (value)
  {
    case effect_type::Rocket_Explosion: return "Rocket_Explosion";
    case effect_type::Bullet_Impact: return "Bullet_Impact";
    case effect_type::Footstep: return "Footstep";
    case effect_type::Jump: return "Jump";
    case effect_type::Land: return "Land";
    case effect_type::Flesh_Impact: return "Flesh_Impact";
  }
  assert(false && "invalid effect_type");
  return "";
}

void fire_rocket_explosion(event_stream_t& stream, const Rocket_Explosion& payload)
{
  stream.writer.write_bits((uint32_t)effect_type::Rocket_Explosion, 16);
  for (const field_info_t& field : Span<const field_info_t>{ROCKET_EXPLOSION_FIELDS, 6})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Rocket_Explosion> try_read_rocket_explosion(network::Bit_Reader& reader)
{
  Rocket_Explosion payload;
  for (const field_info_t& field : Span<const field_info_t>{ROCKET_EXPLOSION_FIELDS, 6})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Rocket_Explosion& value)
{
  return std::string("Rocket_Explosion") + fields_to_text({ROCKET_EXPLOSION_FIELDS, 6}, &value);
}

void fire_bullet_impact(event_stream_t& stream, const Bullet_Impact& payload)
{
  stream.writer.write_bits((uint32_t)effect_type::Bullet_Impact, 16);
  for (const field_info_t& field : Span<const field_info_t>{BULLET_IMPACT_FIELDS, 6})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Bullet_Impact> try_read_bullet_impact(network::Bit_Reader& reader)
{
  Bullet_Impact payload;
  for (const field_info_t& field : Span<const field_info_t>{BULLET_IMPACT_FIELDS, 6})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Bullet_Impact& value)
{
  return std::string("Bullet_Impact") + fields_to_text({BULLET_IMPACT_FIELDS, 6}, &value);
}

void fire_footstep(event_stream_t& stream, const Footstep& payload)
{
  stream.writer.write_bits((uint32_t)effect_type::Footstep, 16);
  for (const field_info_t& field : Span<const field_info_t>{FOOTSTEP_FIELDS, 6})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Footstep> try_read_footstep(network::Bit_Reader& reader)
{
  Footstep payload;
  for (const field_info_t& field : Span<const field_info_t>{FOOTSTEP_FIELDS, 6})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Footstep& value)
{
  return std::string("Footstep") + fields_to_text({FOOTSTEP_FIELDS, 6}, &value);
}

void fire_jump(event_stream_t& stream, const Jump& payload)
{
  stream.writer.write_bits((uint32_t)effect_type::Jump, 16);
  for (const field_info_t& field : Span<const field_info_t>{JUMP_FIELDS, 6})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Jump> try_read_jump(network::Bit_Reader& reader)
{
  Jump payload;
  for (const field_info_t& field : Span<const field_info_t>{JUMP_FIELDS, 6})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Jump& value)
{
  return std::string("Jump") + fields_to_text({JUMP_FIELDS, 6}, &value);
}

void fire_land(event_stream_t& stream, const Land& payload)
{
  stream.writer.write_bits((uint32_t)effect_type::Land, 16);
  for (const field_info_t& field : Span<const field_info_t>{LAND_FIELDS, 6})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Land> try_read_land(network::Bit_Reader& reader)
{
  Land payload;
  for (const field_info_t& field : Span<const field_info_t>{LAND_FIELDS, 6})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Land& value)
{
  return std::string("Land") + fields_to_text({LAND_FIELDS, 6}, &value);
}

void fire_flesh_impact(event_stream_t& stream, const Flesh_Impact& payload)
{
  stream.writer.write_bits((uint32_t)effect_type::Flesh_Impact, 16);
  for (const field_info_t& field : Span<const field_info_t>{FLESH_IMPACT_FIELDS, 6})
    network::write_field(stream.writer, reinterpret_cast<const uint8_t*>(&payload), field, field.offset);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[event fired] {}", to_text(payload));
}

std::optional<Flesh_Impact> try_read_flesh_impact(network::Bit_Reader& reader)
{
  Flesh_Impact payload;
  for (const field_info_t& field : Span<const field_info_t>{FLESH_IMPACT_FIELDS, 6})
    if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&payload), field, field.offset))
      return std::nullopt;
  return payload;
}

std::string to_text(const Flesh_Impact& value)
{
  return std::string("Flesh_Impact") + fields_to_text({FLESH_IMPACT_FIELDS, 6}, &value);
}

std::string effect_stream_to_text(const event_stream_t& stream)
{
  if (stream.empty())
    return "<nothing pending>";

  network::Bit_Reader reader(stream.writer.buffer.data(),
                             stream.writer.buffer.size());
  reader.read_bits(16); // the count slot, backpatched only at send

  std::string text;
  for (uint32_t index = 0; index < stream.count; ++index)
  {
    if (index > 0)
      text += '\n';

    const uint32_t kind = reader.read_bits(16);
    if (kind >= EFFECT_TYPE_COUNT)
    {
      text += std::format("<unknown kind {}; the rest is unreadable>", kind);
      break;
    }

    switch ((effect_type)kind)
    {
      case effect_type::Rocket_Explosion:
      {
        const std::optional<Rocket_Explosion> payload = try_read_rocket_explosion(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case effect_type::Bullet_Impact:
      {
        const std::optional<Bullet_Impact> payload = try_read_bullet_impact(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case effect_type::Footstep:
      {
        const std::optional<Footstep> payload = try_read_footstep(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case effect_type::Jump:
      {
        const std::optional<Jump> payload = try_read_jump(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case effect_type::Land:
      {
        const std::optional<Land> payload = try_read_land(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case effect_type::Flesh_Impact:
      {
        const std::optional<Flesh_Impact> payload = try_read_flesh_impact(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
    }
  }

  return text;
}

} // namespace shared
