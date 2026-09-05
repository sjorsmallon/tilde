#include "entity_inspector.hpp"

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/log.hpp"
#include "imgui.h"
#include <string>
#include <vector>

namespace client
{

namespace
{

// renders a leaf field. components are flattened inside an entity so you can just take offsets and walk the size.
// there's some special casing for quaternions because editing those is easier in euler angles.
void render_leaf_field(uint8_t* base, const entities::leaf_field_t& leaf, int id)
{
  const field_info_t& field = *leaf.info;
  void* field_ptr = base + leaf.offset;
  const char* label = leaf.name.c_str();

  ImGui::PushID(id);

  switch (field.type)
  {
    case FIELD_TYPE_I8:
    case FIELD_TYPE_I16:
    case FIELD_TYPE_I32:
      ImGui::InputInt(label, static_cast<int*>(field_ptr));
      break;

    case FIELD_TYPE_U8:
      ImGui::InputScalar(label, ImGuiDataType_U8, field_ptr);
      break;
    case FIELD_TYPE_U16:
      ImGui::InputScalar(label, ImGuiDataType_U16, field_ptr);
      break;
    case FIELD_TYPE_U32:
      ImGui::InputScalar(label, ImGuiDataType_U32, field_ptr);
      break;
    case FIELD_TYPE_U64:
      ImGui::InputScalar(label, ImGuiDataType_U64, field_ptr);
      break;
    case FIELD_TYPE_I64:
      ImGui::InputScalar(label, ImGuiDataType_S64, field_ptr);
      break;

    case FIELD_TYPE_F32:
      ImGui::DragFloat(label, static_cast<float *>(field_ptr), 0.1f);
      break;
    case FIELD_TYPE_F64:
      ImGui::InputDouble(label, static_cast<double *>(field_ptr));
      break;

    case FIELD_TYPE_BOOL:
      ImGui::Checkbox(label, static_cast<bool *>(field_ptr));
      break;

    case FIELD_TYPE_V3:
      ImGui::DragFloat3(label, static_cast<float *>(field_ptr), 0.1f);
      break;
    case FIELD_TYPE_V4:
      ImGui::DragFloat4(label, static_cast<float *>(field_ptr), 0.1f);
      break;
    case FIELD_TYPE_V4I:
      ImGui::InputInt4(label, static_cast<int *>(field_ptr));
      break;

    case FIELD_TYPE_QUAT:
      edit_rotation_as_euler(label, *static_cast<linalg::quatf *>(field_ptr));
      break;

    case FIELD_TYPE_STRING:
    {
      // pascal_string_t<N> is `uint8 length; char data[N + 1]`, addressed
      // generically through the capacity the field record carries.
      uint8_t *length_byte = static_cast<uint8_t *>(field_ptr);
      char *data = reinterpret_cast<char *>(length_byte + 1);
      if (ImGui::InputText(label, data, field.string_capacity + 1,
                           ImGuiInputTextFlags_EnterReturnsTrue))
      {
        *length_byte = (uint8_t)strlen(data);
        // Restore the canonical zero-padding invariant: ImGui writes a
        // terminator but leaves whatever was past it, and every baseline
        // memcmp would then see a delta that is not there.
        std::memset(data + *length_byte, 0, field.string_capacity + 1 - *length_byte);
      }
      break;
    }

    case FIELD_TYPE_ENUM:
    {
      const enum_type_info_t &info = *field.enum_info;

      // Enum storage is one byte; the widget wants an int.
      uint8_t stored = *static_cast<uint8_t *>(field_ptr);
      int current = (int)stored;

      if (ImGui::Combo(label, &current, info.value_names.data,
                       (int)info.value_names.size()))
      {
        *static_cast<uint8_t *>(field_ptr) = (uint8_t)current;
      }
      break;
    }

    case FIELD_TYPE_ASSET:
    {
      const Span<const assets::asset_info_t> manifest =
          assets::asset_class_manifest(field.asset_class_id);

      std::vector<const char *> names;
      names.reserve(manifest.size());
      for (const assets::asset_info_t &asset : manifest)
        names.push_back(asset.name);

      uint16_t stored = *static_cast<uint16_t *>(field_ptr);
      int current = (int)stored;

      if (ImGui::Combo(label, &current, names.data(), (int)names.size()))
        *static_cast<uint16_t *>(field_ptr) = (uint16_t)current;
      break;
    }

    case FIELD_TYPE_COMPONENT:
    case FIELD_TYPE_INVALID:
      // Components never reach here: the caller flattens them away.
      ImGui::Text("%s: <not an editable leaf>", label);
      break;
  }

  ImGui::PopID();
}

} // namespace

bool edit_rotation_as_euler(const char *label, linalg::quatf &rotation)
{
  struct rotation_edit_t
  {
    const void   *field         = nullptr;
    linalg::quatf last_written  = linalg::quatf::identity();
    linalg::vec3f euler_degrees = {0.f, 0.f, 0.f};
  };
  
  static rotation_edit_t edit;

  const bool same_field_as_previous_edit = edit.field == &rotation;
  const bool stayed_the_same_during_this_edit  = rotation.x == edit.last_written.x && rotation.y == edit.last_written.y &&
                         rotation.z == edit.last_written.z && rotation.w == edit.last_written.w;

  if (!same_field_as_previous_edit || !stayed_the_same_during_this_edit)
  {
    edit.field = &rotation;
    edit.last_written  = rotation;
    edit.euler_degrees = linalg::to_euler_degrees(rotation);
  }

  if (!ImGui::DragFloat3(label, &edit.euler_degrees.x, 1.0f))
    return false;

  rotation = linalg::from_euler_degrees(edit.euler_degrees);
  edit.last_written = rotation;
  return true;
}

void render_entity_fields_in_an_imgui_window(entities::Entity* entity)
{
  if (!entity) return;

  if (entity->type == entities::entity_type::Invalid)
  {
    ImGui::Text("This entity carries an invalid type tag.");
    return;
  }

  const entities::entity_type_info_t& info = entities::entity_info(entity->type);
  ImGui::Text("Class: %s", info.classname);
  ImGui::Separator();

  uint8_t* base = reinterpret_cast<uint8_t*>(entity);

  // @Editable leaves, in declaration order — the same order the map file writes
  // and the .def declares, so the inspector reads like the source of truth does.
  const std::vector<entities::leaf_field_t> leaves =
      entities::collect_leaf_fields(entity->type, entities::FIELD_FLAG_EDITABLE);

  for (size_t index = 0; index < leaves.size(); ++index)
    render_leaf_field(base, leaves[index], (int)index);
}

} // namespace client
