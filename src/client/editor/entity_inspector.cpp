#include "entity_inspector.hpp"
#include "../../shared/entity.hpp"
#include "../../shared/log.hpp"
#include "../../shared/network/schema.hpp"
#include "imgui.h"
#include <algorithm>
#include <string>
#include <vector>

namespace client
{

// Renders a single schema field as ImGui widgets, recursing into nested schemas.
static void render_field_imgui(uint8_t *base_ptr, const network::Field_Prop &field)
{
  if (!has_flag(field.flags, network::Schema_Flags::Editable))
    return;

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
    float *val = static_cast<float *>(field_ptr);
    ImGui::DragFloat3(field.name.c_str(), val, 0.1f);
    break;
  }
  case network::Field_Type::PascalString:
  {
    auto *ps = static_cast<network::pascal_string *>(field_ptr);
    if (field.string_choices_provider)
    {
      // Dropdown rendering: the field declares a callable that returns the
      // valid value set. Lookup the current value's index, fall back to 0
      // if absent (and surface that as a warning -- never silently rebind).
      std::vector<std::string> choices = field.string_choices_provider();
      if (choices.empty())
      {
        // Render visible disabled text rather than nothing — silently
        // rendering an invisible field was the symptom of the
        // static-init-in-static-lib drop that emptied the trigger action
        // registry, and there's no situation where the user benefits from
        // a totally invisible field. No log here; the inspector ticks every
        // frame and would spam.
        ImGui::BeginDisabled();
        ImGui::Text("%s: (no choices available)", field.name.c_str());
        ImGui::EndDisabled();
        break;
      }
      int current_index = -1;
      for (int i = 0; i < static_cast<int>(choices.size()); ++i)
      {
        if (choices[i] == ps->c_str())
        {
          current_index = i;
          break;
        }
      }
      if (current_index < 0)
      {
        log_warning("inspector: field '{}' has value '{}' that is not in its "
                    "choices provider; falling back to index 0",
                    field.name, ps->c_str());
        current_index = 0;
      }
      std::vector<const char *> choice_ptrs;
      choice_ptrs.reserve(choices.size());
      for (const auto &choice : choices)
        choice_ptrs.push_back(choice.c_str());
      if (ImGui::Combo(field.name.c_str(), &current_index, choice_ptrs.data(),
                       static_cast<int>(choice_ptrs.size())))
      {
        ps->set(choices[current_index].c_str());
      }
    }
    else
    {
      if (ImGui::InputText(field.name.c_str(), ps->data, ps->max_length(),
                           ImGuiInputTextFlags_EnterReturnsTrue))
      {
        ps->length = static_cast<network::uint8>(strlen(ps->data));
      }
    }
    break;
  }
  case network::Field_Type::NestedSchema:
  {
    const auto *nested_schema = network::Schema_Registry::get().get_nested_schema(field);
    if (nested_schema && ImGui::TreeNode(field.name.c_str()))
    {
      uint8_t *nested_base = static_cast<uint8_t *>(field_ptr);
      for (const auto &nested_field : nested_schema->fields)
        render_field_imgui(nested_base, nested_field);
      ImGui::TreePop();
    }
    break;
  }
  default:
    ImGui::Text("%s: <Unknown Type>", field.name.c_str());
    break;
  }

  ImGui::PopID();
}

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
    render_field_imgui(base_ptr, field);
}

} // namespace client
