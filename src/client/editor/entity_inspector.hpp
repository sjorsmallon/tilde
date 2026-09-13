#pragma once

#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/map_connection.hpp"
#include "uid_pick.hpp"

#include <optional>

namespace client
{


// Every @Editable leaf of `entity`. With a map, an entity-typed field is a
// dropdown over it plus a Pick button that arms `pick` on that field, so the
// next viewport click writes the uid there -- the connection panel's gesture,
// through the same pick, so the two cannot disagree about what a click does.
// `uid` is the entity's own, what the pick names as its destination.
void render_entity_fields_in_an_imgui_window(entities::Entity *entity,
                                             shared::entity_uid_t uid = shared::null_entity_uid,
                                             const shared::map_t *map = nullptr,
                                             uid_pick_t *pick = nullptr);

// One field's widget, against a FLAT record: the pointer is already the field's
// own bytes and the label is the caller's. Entity leaves compose an offset
// before calling it; a connection's payload override hands it the action's own
// table, whose offsets are final. Both regimes get the same widget per type,
// which is the point -- an override typed against a second set of widgets is a
// second answer to what a v3 looks like.
// An entity-uid field is a dropdown over `map` when one is given, a number otherwise.
void render_field_widget(void *field_bytes, const field_info_t &field, const char *label,
                         int id, const shared::map_t* map = nullptr);

// Every entity in the map plus "(nobody)"; true when the author picked one this frame.
bool draw_entity_uid_combo(const shared::map_t& map, const char* label, shared::entity_uid_t& uid,
                           std::optional<entities::entity_action> annotate_refusal_of = std::nullopt);
// rotation is stored as quaternions, but it's nice to audit using euler angles.
bool edit_rotation_as_euler(const char *label, linalg::quatf &rotation);

} // namespace client
