#include "connection_panel.hpp"

#include "../../shared/entities/generated/entity_io_generated.hpp"
#include "../../shared/log.hpp"
#include "../../shared/map_connection.hpp"
#include "entity_inspector.hpp"
#include "imgui.h"
#include "transaction_system.hpp"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace client
{

namespace
{

const ImVec4 error_color{1.0f, 0.35f, 0.35f, 1.0f};
const ImVec4 pick_color{0.55f, 0.85f, 1.0f, 1.0f};

// Which row the detail editor is showing, and the whole-list snapshot an
// in-progress edit diffs against. Both are per-panel and there is one panel, so
// they are statics here for the reason the cvar panel's filter buffer is one:
// they are UI position, not map data, and nothing else may read them.
size_t s_selected_row = SIZE_MAX;

// Held across the frames of one ImGui interaction. Seeded while nothing is
// active and committed the frame after the author lets go, so a delay drag is
// ONE undo entry rather than one per frame -- the map cvars panel buys the same
// property with IsItemDeactivatedAfterEdit, which does not compose over a
// payload's worth of generated widgets.
std::optional<std::vector<shared::connection_t>> s_edit_baseline;

std::vector<entities::entity_signal> signals_emitted_by(entities::entity_type type)
{
  std::vector<entities::entity_signal> emitted;
  for (uint32_t index = 0; index < entities::ENTITY_SIGNAL_COUNT; ++index)
  {
    const entities::entity_signal signal = (entities::entity_signal)index;
    if (entities::type_emits_signal(type, signal))
      emitted.push_back(signal);
  }
  return emitted;
}

// Whether this row's receiver could accept this action, by the same three rules
// validate_map_connections judges it by -- exact for a Uid and for Self, "some
// type admitted by `by`" for an Activator. Sharing the rule is what stops the
// dropdown offering a value the loader then refuses.
bool receiver_accepts(const shared::map_t &map, const shared::connection_t &row,
                      const entities::Entity &sender, entities::entity_action action)
{
  switch (row.target_kind)
  {
  case shared::connection_target_t::Self:
    return entities::type_accepts_action(sender.type, action);

  case shared::connection_target_t::Unbound:
    return false;

  case shared::connection_target_t::Uid:
  {
    const shared::map_entity_t *entry = map.find_by_uid(row.target);
    if (entry == nullptr || !entry->entity)
      return false;
    return entities::type_accepts_action(entry->entity->type, action);
  }

  case shared::connection_target_t::Activator:
  {
    const uint64_t activators = entities::SIGNAL_ACTIVATOR_MASKS[(uint16_t)row.signal];
    for (uint32_t type = 0; type < entities::ENTITY_TYPE_COUNT; ++type)
    {
      if ((activators & (1ull << type)) == 0)
        continue;
      if (entities::type_accepts_action((entities::entity_type)type, action))
        return true;
    }
    return false;
  }
  }
  return false;
}


// The row as the design doc writes one: sender's signal, an arrow, the receiver,
// the verb. The delay and the fire-once ride in parentheses because a row
// without them is the common one and should read short.
std::string summarize(const shared::map_t &map, const shared::connection_t &row)
{
  std::string line = std::format("{} -> {} {}", entities::to_string(row.signal),
                                 describe_connection_target(map, row), entities::to_string(row.data.tag));

  if (row.delay_seconds > 0.0f && row.fire_once)
    line += std::format("  ({:.2f}s, once)", row.delay_seconds);
  else if (row.delay_seconds > 0.0f)
    line += std::format("  ({:.2f}s)", row.delay_seconds);
  else if (row.fire_once)
    line += "  (once)";

  return line;
}

std::string describe_row_as_sentence(const shared::map_t& map, const shared::connection_t& row)
{
  std::string target;
  switch (row.target_kind)
  {
  case shared::connection_target_t::Activator: target = "whoever caused it"; break;
  case shared::connection_target_t::Self:      target = shared::describe_map_entity(map, row.sender) + " itself"; break;
  case shared::connection_target_t::Unbound:   target = "a target not picked yet (unbound)"; break;
  case shared::connection_target_t::Uid:
    target = row.target == shared::null_entity_uid ? std::string("nobody yet") : shared::describe_map_entity(map, row.target);
    break;
  }

  std::string sentence = std::format("When {} emits {}, the server tells {} to {}",
                                     shared::describe_map_entity(map, row.sender), entities::to_string(row.signal),
                                     target, entities::to_string(row.data.tag));
  if (row.delay_seconds > 0.0f)
    sentence += std::format(", {:.2f} seconds later", row.delay_seconds);
  sentence += ".";
  if (row.fire_once)
    sentence += " Only the first time.";
  return sentence;
}

// The union's bytes belong to whichever tag last wrote them, so a tag change
// has to clear the ones the NEW tag names -- otherwise a Set_Health amount is
// read back as a Damage amount nobody typed. Zero rather than the payload's
// declared defaults: the type is only known at runtime here, and every current
// payload's default is zero anyway.
void retag_payload(shared::connection_t &row, entities::entity_action action)
{
  row.data     = entities::action_data_t{};
  row.data.tag = action;
  std::memset(entities::action_payload_bytes(row.data), 0,
              entities::action_payload_size(action));
}

// A row the panel produced is a row the loader accepts, as far as the panel can
// arrange: a signal and an action whose parameters do not line up NEEDS an
// override, so choosing one turns it on rather than leaving the row red with a
// checkbox the author has to find.
void force_override_when_payloads_disagree(shared::connection_t &row)
{
  if (!shared::signal_payload_passes_through(row.signal, row.data.tag))
    row.has_override = true;
}

// Whether this row names a receiver at all. A `Uid` row on a fresh connection
// names uid 0, and there is nothing to ask about a receiver that is not there --
// which is different from a receiver that refuses, and has to read differently.
bool row_has_a_receiver(const shared::map_t &map, const shared::connection_t &row)
{
  if (row.target_kind == shared::connection_target_t::Unbound)
    return false;
  if (row.target_kind != shared::connection_target_t::Uid)
    return true;
  const shared::map_entity_t *entry = map.find_by_uid(row.target);
  return entry != nullptr && entry->entity;
}

// The ONE place a Uid target is written, so the pick and the dropdown cannot
// come to different conclusions about what changing it means.
//
// A row that had NO receiver was carrying the union's default verb, which is a
// value nobody chose; giving it the first one the new target accepts is what
// makes add-then-click produce a row that already works. A row that HAD one is
// carrying the author's own choice, and a retarget is not permission to replace
// it -- the refusal line says so instead.
void retarget_row(const shared::map_t &map, shared::connection_t &row,
                  shared::entity_uid_t target)
{
  // An Unbound row has no receiver but DOES carry the author's verb -- it is a
  // prefab's row waiting for its target -- so it keeps it.
  const bool was_unaimed =
      row.target_kind != shared::connection_target_t::Unbound && !row_has_a_receiver(map, row);

  row.target_kind = shared::connection_target_t::Uid;
  row.target      = target;

  if (!was_unaimed)
    return;

  const shared::map_entity_t *entry = map.find_by_uid(target);
  if (entry == nullptr || !entry->entity)
    return;

  for (uint32_t index = 0; index < entities::ENTITY_ACTION_COUNT; ++index)
  {
    const entities::entity_action action = (entities::entity_action)index;
    if (!entities::type_accepts_action(entry->entity->type, action))
      continue;
    retag_payload(row, action);
    force_override_when_payloads_disagree(row);
    return;
  }
}

void draw_signal_combo(shared::connection_t &row, const entities::Entity &sender)
{
  const std::vector<entities::entity_signal> emitted = signals_emitted_by(sender.type);

  if (ImGui::BeginCombo("signal", entities::to_string(row.signal)))
  {
    for (entities::entity_signal signal : emitted)
    {
      const bool selected = signal == row.signal;
      if (ImGui::Selectable(entities::to_string(signal), selected) && !selected)
      {
        row.signal = signal;
        force_override_when_payloads_disagree(row);
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
}

void draw_target_kind_combo(shared::connection_t &row)
{
  const shared::connection_target_t kinds[] = {shared::connection_target_t::Uid,
                                               shared::connection_target_t::Activator,
                                               shared::connection_target_t::Self,
                                               shared::connection_target_t::Unbound};

  if (!ImGui::BeginCombo("target kind", shared::to_string(row.target_kind)))
    return;

  for (shared::connection_target_t kind : kinds)
  {
    // A signal whose trait declared no `by` has no type to check an activator
    // against, so the loader refuses the row outright -- offering it here would
    // be offering a row that can never load.
    const bool has_activators =
        entities::SIGNAL_ACTIVATOR_MASKS[(uint16_t)row.signal] != 0;
    const bool unavailable =
        kind == shared::connection_target_t::Activator && !has_activators;

    ImGui::BeginDisabled(unavailable);
    if (ImGui::Selectable(shared::to_string(kind), kind == row.target_kind))
      row.target_kind = kind;
    ImGui::EndDisabled();

    if (unavailable && ImGui::IsItemHovered())
      ImGui::SetTooltip("%s declares no `by` types, so nothing can target its activator.",
                        entities::to_string(row.signal));
    else if (kind == shared::connection_target_t::Unbound && ImGui::IsItemHovered())
      ImGui::SetTooltip("A slot: the target is picked where this is placed. Saving a prefab "
                        "writes one for every row aimed outside it; the loader refuses the "
                        "row until it is picked.");
  }
  ImGui::EndCombo();
}

void draw_target_entity_combo(const shared::map_t &map, shared::connection_t &row,
                             size_t row_index, connection_pick_t &pick)
{
  // Every entity is offered, and one that does not accept the current verb
  // says so rather than disappearing: picking the target first and the verb
  // second is the order an author works in, and a target that vanished
  // would look like a missing entity.
  const char* pick_label = "Pick a target in the viewport";
  const ImGuiStyle& style = ImGui::GetStyle();
  const float pick_button_width =
      ImGui::CalcTextSize(pick_label).x + style.FramePadding.x * 2.0f + style.ItemSpacing.x;

  // An Unbound row's `target` is a key from another map, and a uid that happens
  // to exist here is a coincidence the combo must not show as a choice made.
  shared::entity_uid_t picked = row.target_kind == shared::connection_target_t::Unbound
                                    ? shared::null_entity_uid
                                    : row.target;
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - pick_button_width);
  if (draw_entity_uid_combo(map, "##target_entity", picked, row.data.tag))
    retarget_row(map, row, picked);

  ImGui::SameLine();
  if (ImGui::Button(pick_label))
  {
    pick.armed = true;
    pick.row   = row_index;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Then click the target in the viewport. The click sets the "
                      "target and does not change the selection; Escape cancels.");
}

void draw_action_combo(const shared::map_t &map, shared::connection_t &row,
                       const entities::Entity &sender)
{
  // With no target yet, the list is UNFILTERED. Nothing to check against is not
  // the same fact as "your target refuses this", and hiding everything would
  // leave an empty dropdown that says nothing. It also admits the other
  // authoring order: pick the verb, then find something that takes it, which the
  // target list already annotates for.
  const bool have_receiver = row_has_a_receiver(map, row);

  // What the target accepts, and nothing else. A refused verb used to be listed
  // and greyed with a tooltip saying why -- which is a sentence about a choice
  // that was never available, printed once per verb the target does not have.
  // The list of what you CAN do is the whole answer.
  std::vector<entities::entity_action> offered;
  for (uint32_t index = 0; index < entities::ENTITY_ACTION_COUNT; ++index)
  {
    const entities::entity_action action = (entities::entity_action)index;

    // The row's own verb is always in its own list, even where the target
    // refuses it: a retarget deliberately KEEPS the author's choice, so a
    // stale one is representable, and a combo whose open list does not contain
    // the value in its box reads as a bug rather than as a refusal. The red
    // line below is what says it is wrong.
    if (action == row.data.tag || !have_receiver || receiver_accepts(map, row, sender, action))
      offered.push_back(action);
  }

  if (ImGui::BeginCombo("action", entities::to_string(row.data.tag)))
  {
    for (entities::entity_action action : offered)
    {
      const bool selected = action == row.data.tag;
      if (ImGui::Selectable(entities::to_string(action), selected) && !selected)
      {
        retag_payload(row, action);
        force_override_when_payloads_disagree(row);
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  // A list narrowed to nothing has to say so. Silence there is the failure the
  // whole panel exists to remove -- a dropdown holding only the verb you cannot
  // use is a dead end, not a constraint.
  if (!have_receiver)
    ImGui::TextDisabled("No target yet, so every action is offered. Pick one and the "
                        "list narrows to what it accepts.");
  else if (!receiver_accepts(map, row, sender, row.data.tag) && offered.size() == 1)
    ImGui::TextColored(error_color, "%s accepts no actions at all -- this row can "
                                    "never do anything.",
                       describe_connection_target(map, row).c_str());
  else if (!receiver_accepts(map, row, sender, row.data.tag))
    ImGui::TextColored(error_color, "%s does not accept %s. Pick one it does.",
                       describe_connection_target(map, row).c_str(),
                       entities::to_string(row.data.tag));
}

// WHERE the action's arguments come from. Two sources, named -- not one source
// plus a modifier: `has_override` off is a memcpy of the signal's own payload
// into the action, on is the constant typed on this row. Spelled as a checkbox
// ("override the parameters") it read as overriding a DEFAULT, and there is no
// default; the disabled-and-ticked state a payload mismatch forces then looked
// like a bug rather than like the only legal answer.
void draw_payload_editor(const shared::map_t& map, shared::connection_t &row)
{
  const Span<const field_info_t> fields = entities::action_payload_fields(row.data.tag);

  if (fields.empty())
  {
    ImGui::TextDisabled("%s takes no parameters.", entities::to_string(row.data.tag));
    return;
  }

  const bool passes_through =
      shared::signal_payload_passes_through(row.signal, row.data.tag);

  ImGui::TextUnformatted("Arguments");

  int source = row.has_override ? 1 : 0;

  ImGui::BeginDisabled(!passes_through);
  ImGui::RadioButton(std::format("From {} (its own payload)", entities::to_string(row.signal)).c_str(),
                     &source, 0);
  ImGui::EndDisabled();

  // The reason sits UNDER the option it disables rather than in a tooltip: this
  // is the one case where the author has no choice, and a hover is easy to miss.
  if (!passes_through)
  {
    ImGui::Indent();
    ImGui::TextColored(error_color, "%s and %s do not take the same parameters.",
                       entities::to_string(row.signal), entities::to_string(row.data.tag));
    ImGui::Unindent();
  }

  ImGui::RadioButton("Typed on this row", &source, 1);

  row.has_override = passes_through ? source == 1 : true;

  if (!row.has_override)
    return;

  uint8_t *payload = entities::action_payload_bytes(row.data);
  ImGui::Indent();
  for (uint32_t index = 0; index < fields.size(); ++index)
  {
    const field_info_t &field = fields[index];
    render_field_widget(payload + field.offset, field, field.name, (int)index, &map);
  }
  ImGui::Unindent();
}

// Every row anywhere in the map that fires INTO this entity. Read-only on
// purpose: it is the answer to "why did nothing turn this lamp on", and editing
// it here would be editing another entity's outputs from inside this one's
// panel.
void draw_inbound_list(const shared::map_t &map, shared::entity_uid_t selected_uid)
{
  int shown = 0;
  for (const shared::connection_t &row : map.connections)
  {
    if (row.target_kind != shared::connection_target_t::Uid || row.target != selected_uid)
      continue;

    ImGui::BulletText("%s  %s -> %s", shared::describe_map_entity(map, row.sender).c_str(),
                      entities::to_string(row.signal), entities::to_string(row.data.tag));
    ++shown;
  }

  if (shown == 0)
    ImGui::TextDisabled("(nothing in this map fires into it by uid)");
}

} // namespace

std::string describe_connection_target(const shared::map_t &map, const shared::connection_t &row)
{
  switch (row.target_kind)
  {
  case shared::connection_target_t::Activator: return "!activator";
  case shared::connection_target_t::Self:      return "!self";
  case shared::connection_target_t::Unbound:   return "<unbound>";
  case shared::connection_target_t::Uid:
    return row.target == shared::null_entity_uid
               ? std::string("<no target>")
               : shared::describe_map_entity(map, row.target);
  }
  return "<unknown>";
}

void commit_picked_connection_target(shared::map_t &map, Transaction_System &transactions,
                                     Span<const size_t> rows, shared::entity_uid_t target)
{
  std::vector<shared::connection_t> before = map.connections;

  size_t written = 0;
  for (size_t row : rows)
  {
    if (row >= map.connections.size())
    {
      log_error("connection_panel: a pick named row {} of {}; skipped", row,
                map.connections.size());
      continue;
    }
    retarget_row(map, map.connections[row], target);
    ++written;
  }
  if (written == 0)
    return;

  transaction_t transaction;
  transaction.add_map_connections_modified(std::move(before), map.connections);
  transactions.push(std::move(transaction));
}

void draw_connection_panel(shared::map_t &map, shared::entity_uid_t selected_uid,
                           Transaction_System &transactions, connection_pick_t &pick)
{
  const shared::map_entity_t *entry = map.find_by_uid(selected_uid);
  if (entry == nullptr || !entry->entity)
  {
    pick.disarm();
    s_edit_baseline.reset();
    return;
  }

  const entities::Entity &sender = *entry->entity;

  // Seeded while nothing is being dragged; retained across the frames of one
  // interaction, which is what makes a whole drag one undo entry.
  if (!s_edit_baseline)
    s_edit_baseline = map.connections;

  const std::vector<shared::connection_refusal_t> refusals = shared::validate_map_connections(map);
  const auto reasons_for = [&](size_t index)
  {
    std::vector<const std::string *> reasons;
    for (const shared::connection_refusal_t &refusal : refusals)
      if (refusal.index == index)
        reasons.push_back(&refusal.reason);
    return reasons;
  };

  std::vector<size_t> outbound;
  for (size_t index = 0; index < map.connections.size(); ++index)
    if (map.connections[index].sender == selected_uid)
      outbound.push_back(index);

  const bool selection_is_live =
      std::find(outbound.begin(), outbound.end(), s_selected_row) != outbound.end();
  if (!selection_is_live)
    s_selected_row = outbound.empty() ? SIZE_MAX : outbound.front();

  // An armed pick names the row being filled, so that is the row to show -- a
  // stamp arms it on the prefab's UNBOUND row, which is rarely the sender's
  // first.
  if (pick.armed && std::find(outbound.begin(), outbound.end(), pick.row) != outbound.end())
    s_selected_row = pick.row;

  int row_to_remove = -1;
  bool add_requested = false;

  if (ImGui::Begin("Connections", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
  {
    ImGui::TextUnformatted(shared::describe_map_entity(map, selected_uid).c_str());

    const std::vector<entities::entity_signal> emitted = signals_emitted_by(sender.type);
    if (emitted.empty())
      ImGui::TextDisabled("%s announces nothing: just a target. Not a Sender.",
                          entities::entity_info(sender.type).classname);

    if (pick.armed)
      ImGui::TextColored(pick_color,
                         "Pick a target: click an entity in the viewport (Escape cancels).");

    ImGui::SeparatorText("Outputs");

    if (outbound.empty())
    {
      ImGui::TextDisabled("(this entity fires nothing)");
    }
    else
    {
      if (ImGui::BeginChild("##outbound", ImVec2(0, 110), true))
      {
        for (size_t index : outbound)
        {
          const shared::connection_t &row = map.connections[index];
          const bool refused = !reasons_for(index).empty();

          ImGui::PushID((int)index);
          if (refused)
            ImGui::PushStyleColor(ImGuiCol_Text, error_color);
          if (ImGui::Selectable(summarize(map, row).c_str(), index == s_selected_row))
            s_selected_row = index;
          if (refused)
            ImGui::PopStyleColor();
          ImGui::PopID();
        }
      }
      ImGui::EndChild();
    }

    ImGui::BeginDisabled(emitted.empty());
    if (ImGui::Button("+ Add connection"))
      add_requested = true;
    ImGui::EndDisabled();

    if (s_selected_row < map.connections.size())
    {
      shared::connection_t &row = map.connections[s_selected_row];

      ImGui::SeparatorText("Selected row");
      ImGui::TextWrapped("%s", describe_row_as_sentence(map, row).c_str());
      ImGui::Spacing();
      ImGui::PushID((int)s_selected_row);

      draw_signal_combo(row, sender);
      draw_target_kind_combo(row);
      if (row.target_kind == shared::connection_target_t::Uid ||
          row.target_kind == shared::connection_target_t::Unbound)
        draw_target_entity_combo(map, row, s_selected_row, pick);
      draw_action_combo(map, row, sender);
      draw_payload_editor(map, row);

      ImGui::DragFloat("delay (s)", &row.delay_seconds, 0.01f, 0.0f, 600.0f, "%.2f");
      ImGui::Checkbox("fire once", &row.fire_once);

      // What the loader would say, said where the author is standing -- the rule
      // a brush with no collision already follows. build_session drops these
      // rows and the server refuses the whole map, and neither is visible from a
      // viewport.
      for (const std::string *reason : reasons_for(s_selected_row))
        ImGui::TextColored(error_color, "Refused: %s", reason->c_str());

      if (ImGui::Button("Remove this connection"))
        row_to_remove = (int)s_selected_row;

      ImGui::PopID();
    }

    ImGui::SeparatorText("Inbound (read-only)");
    draw_inbound_list(map, selected_uid);
  }
  ImGui::End();

  // Structural edits are deferred to here for the reason the cvar panel defers
  // its own: both of them replace the vector the loop above was walking.
  if (add_requested)
  {
    const std::vector<entities::entity_signal> emitted = signals_emitted_by(sender.type);
    if (!emitted.empty())
    {
      shared::connection_t row;
      row.sender      = selected_uid;
      row.signal      = emitted.front();
      row.target_kind = shared::connection_target_t::Uid;
      force_override_when_payloads_disagree(row);

      map.connections.push_back(row);
      s_selected_row = map.connections.size() - 1;

      // The new row has no target and is refused until it gets one, so the pick
      // is armed rather than left for the author to find: add-then-click is the
      // gesture, and the red row in between is what says why it is not done.
      pick.armed = true;
      pick.row   = s_selected_row;
    }
  }
  else if (row_to_remove >= 0)
  {
    map.connections.erase(map.connections.begin() + row_to_remove);
    s_selected_row = SIZE_MAX;
    pick.armed     = false;
  }

  if (pick.row >= map.connections.size())
    pick.armed = false;

  // Nothing is being held, so whatever the frame changed is finished. One
  // transaction for the whole interaction, and a no-op when it changed nothing.
  if (!ImGui::IsAnyItemActive())
  {
    transaction_t transaction;
    transaction.add_map_connections_modified(std::move(*s_edit_baseline), map.connections);
    transactions.push(std::move(transaction));
    s_edit_baseline.reset();
  }
}

} // namespace client
