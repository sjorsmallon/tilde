#include "entity_inspector.hpp"
#include "../../shared/entity.hpp"
#include "../../shared/network/schema.hpp"
#include "imgui.h"
#include <string>

namespace client
{

void render_imgui_entity_fields_in_a_window(network::Entity *entity)
{
  if (!entity)
    return;

  const auto *schema = entity->get_schema();
  if (!schema)
  {
    ImGui::Text("No schema available for this entity.");
    return;
  }

  ImGui::Text("Class: %s", schema->class_name.c_str());
  ImGui::Separator();

  uint8_t *base_ptr = reinterpret_cast<uint8_t *>(entity);

  for (const auto &field : schema->fields)
  {
    void *field_ptr = base_ptr + field.offset;

    ImGui::PushID(field.index);

    switch (field.type)
    {
    case network::Field_Type::Int32:
    {
      int *val = static_cast<int *>(field_ptr);
      ImGui::InputInt(field.name.c_str(), val);
      break;
    }
    case network::Field_Type::Float32:
    {
      float *val = static_cast<float *>(field_ptr);
      ImGui::DragFloat(field.name.c_str(), val, 0.1f);
      break;
    }
    case network::Field_Type::Bool:
    {
      bool *val = static_cast<bool *>(field_ptr);
      ImGui::Checkbox(field.name.c_str(), val);
      break;
    }
    case network::Field_Type::Vec3f:
    {
      // Assumes vector is 3 floats (linalg::vec3 or similar layout)
      float *val = static_cast<float *>(field_ptr);
      ImGui::DragFloat3(field.name.c_str(), val, 0.1f);
      break;
    }
    default:
      ImGui::Text("%s: <Unknown Type>", field.name.c_str());
      break;
    }

    ImGui::PopID();
  }
}

} // namespace client
