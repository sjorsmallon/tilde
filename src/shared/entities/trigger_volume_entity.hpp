#pragma once

#include "../entity.hpp"

namespace network
{

enum class trigger_action : int32
{
  kill = 0,
};

class Trigger_Volume_Entity : public Entity
{
public:
  SCHEMA_FIELD(vec3f, half_extents,
               Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);

  SCHEMA_FIELD_DEFAULT(int32, action,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       static_cast<int32>(trigger_action::kill));

  DECLARE_SCHEMA(Trigger_Volume_Entity)
};

SCHEMA_NAME_FOR_TYPE(Trigger_Volume_Entity)

} // namespace network
