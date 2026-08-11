#include "animation_tool.hpp"

#include "../../../shared/cvars/generated/cvars_generated.hpp"
#include "../../../shared/entities/generated/entities_generated.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/model_format.hpp"
#include "../../../shared/player_constants.hpp"
#include "../../../shared/hit_region.hpp"
#include "../../../shared/player_animator.hpp"
#include "../../../shared/skinning.hpp"
#include "../../hitbox_debug_draw.hpp"
#include "../../renderer.hpp"
#include "../../state_manager.hpp"
#include "../entity_editor_traits.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>

namespace client
{
namespace
{

constexpr entities::mesh_asset PREVIEW_MESH = entities::mesh_asset::Leet_Full;

// A leaf bone has no child to draw toward, so its segment is a stub along the
// bone's own local +Y (its height). Long enough to see which way the bone points.
constexpr float LEAF_BONE_STUB_LENGTH = 4.0f;


// used to resolve all subitems for this mesh. the animations / hitboxes / armature.
constexpr const char *MODELS_DIRECTORY = "resources/models/";

// The bare filename of a clip path, for a combo label. Returns a pointer INTO
// `path`, so it lives exactly as long as the `clip_paths` entry it came from --
// which is every use here, since ImGui copies the label it is handed.
const char *clip_name_of(const std::string &path)
{
  const size_t slash = path.find_last_of('/');
  return path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}

} // namespace

void Animation_Tool::on_enable(editor_context_t &ctx)
{
  selected_bone   = Animation_Tool::NO_BONE_SELECTED;
  selected_volume = Animation_Tool::NO_VOLUME_SELECTED;

  // The pose has to exist before the rig can be resolved against it, and
  // update_pose is what resolves the skeleton -- so this is two calls, in this
  // order, not one.
  if (update_pose())
    load_rig();

  scan_clips();
}

void Animation_Tool::on_disable(editor_context_t &ctx) {}

void Animation_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view, float dt)
{
  // Before update_pose, so the pose drawn this frame is the phase the clock just
  // produced rather than the previous frame's.
  advance_clip(dt);

  const bool posed = update_pose();

  // The rig resolves bone names against the skeleton, so the frame the model
  // first becomes available is also the first frame the rig can load.
  if (posed && rig.volumes.empty() && !rig_load_attempted)
    load_rig();
}

void Animation_Tool::scan_clips()
{
  const std::string previous =
      (selected_clip != NO_CLIP_SELECTED) ? clip_paths[selected_clip] : std::string();

  clip_paths.clear();
  selected_clip = NO_CLIP_SELECTED;

  std::error_code error_code;
  std::filesystem::directory_iterator directory(MODELS_DIRECTORY, error_code);
  if (error_code)
  {
    log_error("[animation] could not list '{}' for clips: {}", MODELS_DIRECTORY,
              error_code.message());
    return;
  }

  for (const std::filesystem::directory_entry &entry : directory)
  {
    if (entry.is_regular_file() && entry.path().extension() == ".animation")
      clip_paths.push_back(entry.path().generic_string());
  }
  std::sort(clip_paths.begin(), clip_paths.end());

  // Hold the selection across a rescan. Re-exporting the clip you are watching
  // is the normal loop, and dropping back to clip 0 every time would make the
  // Reload button useless for the one thing it exists for.
  for (size_t index = 0; index < clip_paths.size(); ++index)
  {
    if (clip_paths[index] == previous)
    {
      selected_clip = (int)index;
      break;
    }
  }
  if (selected_clip == NO_CLIP_SELECTED && !clip_paths.empty())
    selected_clip = 0;

  if (selected_clip != NO_CLIP_SELECTED)
    load_selected_clip();
}

void Animation_Tool::load_selected_clip()
{
  clip_handle = {};
  clip_phase  = 0.0f;

  if (selected_clip == NO_CLIP_SELECTED)
    return;

  clip_handle = assets::load_animation(clip_paths[selected_clip].c_str());
}

void Animation_Tool::advance_clip(float dt)
{
  if (pose_source != Pose_Source::Clip || !clip_playing)
    return;

  const assets::animation_clip_t *clip = assets::get(clip_handle);
  if (!clip)
    return;

  // A one-frame clip one-shot has no duration, so there is nothing to advance
  // and nothing to divide by -- it is already showing its only pose.
  const float duration = assets::clip_duration_seconds(*clip, clip_looping);
  if (!(duration > 0.0f))
    return;

  clip_phase += dt * clip_playback_speed / duration;

  if (clip_looping)
  {
    clip_phase -= std::floor(clip_phase);
    return;
  }

  // A one-shot stops ON the last frame rather than snapping back, so what you
  // are left looking at is the pose the clip ends in.
  if (clip_phase >= 1.0f)
  {
    clip_phase   = 1.0f;
    clip_playing = false;
  }
  else if (clip_phase < 0.0f) // reachable with a negative speed
  {
    clip_phase   = 0.0f;
    clip_playing = false;
  }
}

void Animation_Tool::on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e) {}
void Animation_Tool::on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e) {}
void Animation_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e) {}
void Animation_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e) {}

linalg::vec3f Animation_Tool::bone_head(uint32_t bone_index) const
{
  // Column-major: cols[3] is the translation column.
  const linalg::vec4 &translation = model_space_matrices[bone_index][3];
  return linalg::vec3f{translation.x, translation.y, translation.z};
}

linalg::vec3f Animation_Tool::to_world_direction(const linalg::vec3f &model_space_vector) const
{
  // Ry at the model-yaw slider's angle -- the same rotation renderer.cpp applies
  // to the mesh (T * Rz * Ry * Rx * S, with pitch and roll zero here). Sweeps +X
  // toward -Z, the OPPOSITE of the viewangle yaw in linalg.hpp; the mesh's model
  // matrix is the convention that has to match.
  constexpr float DEGREES_TO_RADIANS = 3.14159265358979f / 180.0f;
  const float     angle              = model_yaw_degrees * DEGREES_TO_RADIANS;
  const float     cosine             = std::cos(angle);
  const float     sine               = std::sin(angle);
  //@NOTE(SJM): rotate around +y from the model yaw.
  // x is transformed as a function of the cosine of x because the rotation is expressed
  // as 0 _from the x-axis_ in the xz-plane. however, x loses as a function of x, but gains as a function of z.
  // "how much of this x is now expressed in z" or whatever intuition you might have.
  // the cosine of x is that x is most of x (cos(0) = 1, if x is x. (in the x/z plane. :~))
  return {cosine * model_space_vector.x + sine * model_space_vector.z, model_space_vector.y,
          -sine * model_space_vector.x + cosine * model_space_vector.z};
}

linalg::vec3f Animation_Tool::to_world(const linalg::vec3f &model_space_point) const
{
  // The model stands at the world origin, so there is no translation to add and
  // a point transforms exactly like a direction. Separate anyway, so call sites
  // commit to which one they mean.
  return to_world_direction(model_space_point);
}

void Animation_Tool::load_rig()
{
  rig  = {};
  resolved_rig.clear();
  hitboxes.clear();
  seeds.clear();
  coverage = {};
  excursion = {};
  rig_load_attempted = true;
  selected_volume    = NO_VOLUME_SELECTED;

  if (!skeleton)
  {
    log_error("[hitbox] no skeleton is resolved, so there is nothing to resolve bone names against");
    return;
  }

  rig_path = std::string(MODELS_DIRECTORY) + skeleton->name + ".hitboxes";

  // Both loaders name the file, the volume and the bones they could not find,
  // so a failure here has already said everything there is to say.
  std::optional<assets::hitbox_rig_t> parsed = models::try_parse_hitbox_rig_file(rig_path.c_str());
  if (!parsed)
    return;

  std::optional<assets::resolved_hitbox_rig_t> resolved =
      assets::try_resolve_hitbox_rig(*parsed, *skeleton);
  if (!resolved)
    return;

  rig          = std::move(*parsed);
  resolved_rig = std::move(*resolved);
  hitboxes.resize(rig.volumes.size());

  refresh_derivation();
}

void Animation_Tool::refresh_derivation()
{
  const assets::mesh_asset_t *mesh = assets::get(mesh_handle);
  if (!skeleton || !mesh || rig.volumes.empty())
    return;

  seeds.resize(rig.volumes.size());
  assets::derive_hitbox_sizes(*mesh, *skeleton, rig, resolved_rig, seeds);

  // Coverage is a BIND-pose property (skinning moves skin and volumes together),
  // so it is computed against the bind pose (read: t-pose) rather than the one on screen.
  // Both of these walk every vertex against every volume and derivation prints a
  // table, so this runs on edits that change the answer.

  std::vector<linalg::mat4f> bind_parent_space(skeleton->bones.size());
  std::vector<linalg::mat4f> bind_model(skeleton->bones.size());
  assets::compute_parent_space_bind_matrices(*skeleton, bind_parent_space);
  assets::compute_model_space_matrices(*skeleton, bind_parent_space, bind_model);

  std::vector<assets::posed_hitbox_t> bind_hitboxes(rig.volumes.size());
  assets::compute_posed_hitboxes(rig, resolved_rig, bind_model, bind_hitboxes);
  coverage = assets::compute_hitbox_coverage(*mesh, *skeleton, bind_hitboxes,
                                             assets::HITBOX_COVERAGE_TOLERANCE);
}

bool Animation_Tool::update_pose()
{
  // Set pessimistically and cleared on the way out, so a run of failing frames
  // logs once and the frame after a success logs again.
  const bool report    = !model_failure_logged;
  model_failure_logged = true;
  skeleton             = nullptr;

  mesh_handle = assets::get_mesh(PREVIEW_MESH);
  if (!mesh_handle.valid())
  {
    if (report)
      log_error("[animation] preview mesh '{}' did not resolve through the asset manifest",
                entities::to_string(PREVIEW_MESH));
    return false;
  }

  const assets::mesh_asset_t* mesh = assets::get(mesh_handle);
  if (!mesh || !mesh->is_skinned())
  {
    if (report)
      log_error("[animation] preview mesh '{}' has no skin arrays: it exported unskinned",
                entities::to_string(PREVIEW_MESH));
    return false;
  }

  skeleton = assets::get(mesh->skeleton);
  if (!skeleton)
  {
    if (report)
      log_error("[animation] preview mesh '{}' names a skeleton that is not in the cache",
                entities::to_string(PREVIEW_MESH));
    return false;
  }

  const uint32_t bone_count = (uint32_t)skeleton->bones.size();

  switch (pose_source)
  {
    case Pose_Source::Bind:
      assets::compute_bind_pose(*skeleton, pose);
      break;

    case Pose_Source::Single_Pose:
    {
      assets::aim_poses_blend_weights_t blend_weights;
      assets::aim_pose_clips_t          clips;
      for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
      {
        const entities::Aim_Pose pose = (entities::Aim_Pose)index;
        blend_weights.weights[pose]   = (pose == single_pose) ? 1.0f : 0.0f;
        clips[pose]                   = assets::get(holding_gun_aim_poses().poses[pose]);
      }

      assets::sample_aim_pose(pose, clips, blend_weights);
      break;
    }

    case Pose_Source::Aim_Blend:
      compute_aim_pose(holding_gun_aim_poses(), *skeleton, pitch_degrees, yaw_deviation_degrees,
                       aim_settings_from(*state_manager::get_client_context().cvars), pose);
      break;

    case Pose_Source::Clip:
    {
      // Unlike the aim poses -- which are a build the process cannot run without
      // -- a clip is something you point the tool at, so a missing one is a
      // state the panel reports rather than a reason to die.
      const assets::animation_clip_t *clip = assets::get(clip_handle);
      if (!clip)
      {
        if (report)
          log_error("[animation] no clip is loaded; pick one in the Clip panel");
        return false;
      }

      // The clip's bone count is checked against the skeleton below, on the same
      // path every other source takes.
      assets::sample_animation_clip_at(pose, *clip, clip_phase, clip_looping);
      break;
    }
  }

  if (pose.local.size() != bone_count)
  {
    if (report)
      log_error("[animation] the sampled pose has {} bones but skeleton '{}' has {}",
                pose.local.size(), skeleton->name, bone_count);
    return false;
  }

  model_failure_logged = false;

  local_matrices.resize(bone_count);
  model_space_matrices.resize(bone_count);
  skinning_matrices.resize(bone_count);

  assets::get_local_transforms_of_bones_from_pose(pose, local_matrices);
  assets::compute_model_space_matrices(*skeleton, local_matrices, model_space_matrices);
  assets::compute_skinning_matrices(*skeleton, local_matrices, skinning_matrices);

  // The volumes follow the pose every frame, through the same model-space
  // matrices the mesh is skinned by -- which is the whole claim phase B makes.
  if (!hitboxes.empty())
  {
    assets::compute_posed_hitboxes(rig, resolved_rig, model_space_matrices, hitboxes);
    excursion = assets::compute_hull_excursion(hitboxes, shared::player_half_width,
                                               shared::player_half_height * 2.0f);
  }
  return true;
}

std::optional<view_focus_t> Animation_Tool::view_focus() const
{
  // Bone heads, not the movement hull: the hull is a constant, while an
  // extended arm is exactly the thing you snapped to a side view to look at.
  if (!skeleton || model_space_matrices.empty())
    return view_focus_t{.center = {0.0f, shared::player_half_height, 0.0f},
                        .radius = shared::player_half_height};

  linalg::vec3f minimum = bone_head(0);
  linalg::vec3f maximum = minimum;
  for (uint32_t index = 1; index < (uint32_t)model_space_matrices.size(); ++index)
  {
    const linalg::vec3f head = bone_head(index);
    minimum = {std::min(minimum.x, head.x), std::min(minimum.y, head.y),
               std::min(minimum.z, head.z)};
    maximum = {std::max(maximum.x, head.x), std::max(maximum.y, head.y),
               std::max(maximum.z, head.z)};
  }

  const linalg::vec3f center = (minimum + maximum) * 0.5f;
  const linalg::vec3f extent = (maximum - minimum) * 0.5f;

  // Bone heads stop at the wrists and ankles, so the skin reaches past them.
  // A flat radius floor is cheaper than skinning 1216 vertices to find out, and
  // erring wide only costs a little empty viewport.
  constexpr float SKIN_MARGIN = 8.0f;
  const float radius =
      std::max({extent.x, extent.y, extent.z, shared::player_half_height}) + SKIN_MARGIN;
  return view_focus_t{.center = center, .radius = radius};
}

void Animation_Tool::on_draw_overlay(editor_context_t &ctx, overlay_renderer_t &renderer)
{
  // `skeleton` is the one "is there anything to draw" test, and it is set only
  // by on_update. The hull and the static table below do NOT depend on it --
  // they are map-space constants and stay useful when the model is missing.
  const bool posed = skeleton != nullptr;

  if (posed && show_mesh)
  {
    renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle,
                        {.position = {0, 0, 0},
                         .scale    = {1, 1, 1},
                         .rotation = {0, model_yaw_degrees, 0},
                         .color    = colors::white,
                         .shader = unlit ? renderer::shader_type::Unlit : renderer::shader_type::Lit,
                         .wireframe             = wireframe,
                         .skinning_matrices     = skinning_matrices.data(),
                         .skinning_matrix_count = (uint32_t)skinning_matrices.size()});
  }

  if (posed && show_skeleton)
  {
    const uint32_t bone_count = (uint32_t)skeleton->bones.size();

    // A bone's segment runs to each CHILD's head, so a bone with two children
    // fans out and a leaf gets a stub. The skeleton stores no tail, and
    // reconstructing one from the child is what a hitbox span will do in phase
    // B -- same walk, same answer.
    std::vector<bool> has_child(bone_count, false);
    for (uint32_t index = 0; index < bone_count; ++index)
    {
      const int32_t parent = skeleton->bones[index].parent_index;
      if (parent != assets::ROOT_BONE_INDEX)
      {
        has_child[parent] = true;
        const color_t color =
            ((int)parent == selected_bone || (int)index == selected_bone) ? colors::gold : colors::green;
        renderer.draw_line(to_world(bone_head(parent)), to_world(bone_head(index)), color);
      }
    }

    for (uint32_t index = 0; index < bone_count; ++index)
    {
      const linalg::vec3f head     = to_world(bone_head(index));
      const bool          selected = (int)index == selected_bone;

      if (!has_child[index])
      {
        // Where the bone POINTS, which is minus its third column, not its
        // second -- see assets::bone_direction. Drawn along +Y here until
        // 2026-08-10, which pointed every leaf stub sideways.
        const linalg::vec3f axis =
            to_world(bone_head(index) +
                       assets::bone_direction(model_space_matrices[index]) * LEAF_BONE_STUB_LENGTH);
        renderer.draw_line(head, axis, selected ? colors::gold : colors::green);
      }

      // Joints get a marker so a bone that has collapsed onto its parent -- the
      // classic wrong-inverse-bind symptom -- is visible as a doubled dot
      // rather than as nothing at all.
      renderer.draw_wire_aabb(head, {0.6f, 0.6f, 0.6f}, selected ? colors::gold : colors::white);

      if (show_bone_names || selected)
        renderer.draw_text_in_world(head, skeleton->bones[index].name.c_str(), colors::white);
    }
  }

  // The volumes, under whatever pose is selected. Coloured by damage region so
  // an arm that is Torso for damage reads as one at a glance, and the selected
  // row is gold -- the table and the overlay are the same view.
  if (posed && show_hitboxes)
  {
    // Through the same wireframes the in-game overlay uses, so the tool cannot
    // show a shape the game does not.
    const auto line = [&](const linalg::vec3f &start, const linalg::vec3f &end, color_t color)
    { renderer.draw_line(start, end, color); };

    for (uint32_t index = 0; index < (uint32_t)hitboxes.size(); ++index)
    {
      // The same hot-pink/white pulse the Selection tool highlights with, from
      // the same function -- a selection that reads differently in two tools is
      // two conventions. A static highlight would also be ambiguous here, since
      // a volume's region colour is already a colour.
      const assets::posed_hitbox_t &hitbox = hitboxes[index];
      const color_t                 color  = (int)index == selected_volume
                                                 ? compute_selection_pulse_color(ctx.time)
                                                 : hit_region_color(hitbox.region);

      // The model-yaw slider turns the overlay with the mesh, so the volume
      // turns with it too -- endpoints and, for a box, the frame its extents are
      // read in.
      assets::posed_hitbox_t placed = hitbox;
      placed.start                  = to_world(hitbox.start);
      placed.end                    = to_world(hitbox.end);
      placed.frame                  = {to_world_direction(hitbox.frame.right),
                                       to_world_direction(hitbox.frame.up),
                                       to_world_direction(hitbox.frame.forward)};

      draw_posed_hitbox(line, placed, color);

      if ((int)index == selected_volume)
        renderer.draw_text_in_world(placed.start, rig.volumes[index].name.c_str(), colors::white);
    }
  }

  // The movement hull, in the coordinates it is written in: offsets from the
  // FEET, which is the world origin here. It is what the volumes are audited
  // against -- see the hull excursion readout.
  if (show_movement_hull)
  {
    renderer.draw_wire_aabb({0, shared::player_half_height, 0},
                           {shared::player_half_width, shared::player_half_height,
                            shared::player_half_width},
                           colors::white);
  }
}

void Animation_Tool::draw_hitbox_panel()
{
  ImGui::Text("Hit volumes");

  if (rig.volumes.empty())
  {
    ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s",
                       rig_load_attempted ? "no volumes -- see the console" : "no volumes");
    ImGui::TextDisabled("%s", rig_path.c_str());


    const assets::mesh_asset_t *mesh = assets::get(mesh_handle);
    if (skeleton && mesh && ImGui::Button("Write template"))
    {
      const std::string template_path = rig_path + ".template";
      const assets::hitbox_rig_t template_rig =
          assets::make_hitbox_rig_template(*mesh, *skeleton);
      if (models::try_write_hitbox_rig_file(template_path.c_str(), template_rig))
        printf("[hitbox] wrote a template to '%s' -- edit it down and drop the suffix\n",
               template_path.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
      load_rig();
    return;
  }

  const char *worst_volume = excursion.volume_index < 0
                                 ? "none"
                                 : rig.volumes[(size_t)excursion.volume_index].name.c_str();
  const bool over_budget = excursion.distance > assets::HITBOX_MAX_HULL_EXCURSION;
  ImGui::TextColored(over_budget ? ImVec4(1, 0.5f, 0.3f, 1) : ImVec4(0.6f, 0.9f, 0.6f, 1),
                     "hull excursion %.2f / %.1f  (%s)", excursion.distance,
                     assets::HITBOX_MAX_HULL_EXCURSION, worst_volume);

  if (coverage.vertex_count > 0)
  {
    const char *worst_bone =
        coverage.worst_bone < 0 ? "none" : skeleton->bones[(size_t)coverage.worst_bone].name.c_str();
    ImGui::Text("coverage: %u/%u vertices outside every volume, worst %.2f (%s)",
                coverage.uncovered_vertex_count, coverage.vertex_count, coverage.worst_distance,
                worst_bone);
  }

  // list all hitboxes and allow modification. changes in the file can still reflect.
  if (ImGui::BeginTable("volumes", 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
  {
    ImGui::TableSetupColumn("volume");
    ImGui::TableSetupColumn("shape");
    ImGui::TableSetupColumn("span");
    ImGui::TableSetupColumn("region");
    ImGui::TableSetupColumn("size");
    ImGui::TableSetupColumn("derived");
    ImGui::TableHeadersRow();

    for (uint32_t index = 0; index < (uint32_t)rig.volumes.size(); ++index)
    {
      assets::hitbox_volume_t &volume = rig.volumes[index];
      const bool               round  = assets::hitbox_shape_uses_radius(volume.shape);
      ImGui::PushID((int)index);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      if (ImGui::Selectable(volume.name.c_str(), (int)index == selected_volume,
                            ImGuiSelectableFlags_SpanAllColumns))
        selected_volume = ((int)index == selected_volume) ? -1 : (int)index;

      ImGui::TableNextColumn();
      ImGui::TextUnformatted(assets::to_string(volume.shape));

      ImGui::TableNextColumn();
      if (volume.shape == assets::hitbox_shape_t::Sphere)
        ImGui::TextUnformatted(volume.start_bone.c_str());
      else
        ImGui::Text("%s -> %s", volume.start_bone.c_str(), volume.end_bone.c_str());

      ImGui::TableNextColumn();
      ImGui::TextUnformatted(shared::to_string(volume.region));

      ImGui::TableNextColumn();
      if (round)
        ImGui::Text("r %.2f", volume.radius);
      else
        ImGui::Text("%.2f %.2f %.2f", volume.half_extents.x, volume.half_extents.y,
                    volume.half_extents.z);

      ImGui::TableNextColumn();
      if (index < seeds.size())
      {
        if (round)
          ImGui::Text("%.2f", seeds[index].radius);
        else
          ImGui::Text("%.2f %.2f %.2f", seeds[index].half_extents.x, seeds[index].half_extents.y,
                      seeds[index].half_extents.z);
      }
      else
        ImGui::TextDisabled("--");

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (selected_volume >= 0)
  {
    // The selected volume's numbers, wide enough to drag slowly while watching
    // the shape move. The table shows the same values; this is where they are
    // edited, because a column the width of a word is not a place to drag.
    assets::hitbox_volume_t &volume = rig.volumes[(size_t)selected_volume];
    const assets::hitbox_seed_t seed =
        selected_volume < (int)seeds.size() ? seeds[(size_t)selected_volume] : assets::hitbox_seed_t{};

    // Changing shape keeps the span and the region and only reinterprets the
    // size, so a volume seeded as a capsule can be tried as a box without
    // retyping it. A Sphere collapses the span to its start bone, which is what
    // the format means by one bone.
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::BeginCombo("shape", assets::to_string(volume.shape)))
    {
      for (uint32_t index = 0; index < (uint32_t)assets::hitbox_shape_t::Count; ++index)
      {
        const assets::hitbox_shape_t candidate = (assets::hitbox_shape_t)index;
        if (!ImGui::Selectable(assets::to_string(candidate), candidate == volume.shape))
          continue;

        volume.shape = candidate;
        if (candidate == assets::hitbox_shape_t::Sphere)
          volume.end_bone = volume.start_bone;

        // Seed whichever size the new shape reads if it has never been set --
        // a box that starts at zero is invisible, which looks like a bug in the
        // shape switch rather than a number waiting to be typed.
        if (assets::hitbox_shape_uses_radius(candidate) && volume.radius <= 0.0f)
          volume.radius = seed.radius;
        if (!assets::hitbox_shape_uses_radius(candidate) && volume.half_extents.x <= 0.0f)
          volume.half_extents = seed.half_extents;

        refresh_derivation();
      }
      ImGui::EndCombo();
    }

    // Letting go of any of these drags is what re-derives: the size moves the
    // coverage answer and the offset moves the volume the seed is measured from,
    // and neither is worth recomputing on every mouse delta.
    bool edit_finished = false;

    if (assets::hitbox_shape_uses_radius(volume.shape))
    {
      ImGui::SetNextItemWidth(120.0f);
      ImGui::DragFloat("radius", &volume.radius, 0.05f, 0.1f, 64.0f, "%.2f");
      edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SameLine();
      if (ImGui::SmallButton("fill from derived"))
      {
        volume.radius = seed.radius;
        edit_finished = true;
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%.2f", seed.radius);
    }
    else
    {
      // Right, up, along the bone -- the volume's own frame, which is why the
      // labels are not x/y/z.
      ImGui::SetNextItemWidth(220.0f);
      ImGui::DragFloat3("half-extents (right, up, along)", &volume.half_extents.x, 0.05f, 0.1f,
                        64.0f, "%.2f");
      edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SameLine();
      if (ImGui::SmallButton("fill from derived"))
      {
        volume.half_extents = seed.half_extents;
        edit_finished       = true;
      }
      ImGui::TextDisabled("derived %.2f %.2f %.2f", seed.half_extents.x, seed.half_extents.y,
                          seed.half_extents.z);
    }

    // Bone space, so it rotates with the pose. Only the head and the hands want
    // one, which is why it is here rather than in the table.
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("offset along start bone", &volume.offset, 0.05f, -32.0f, 32.0f, "%.2f");
    edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

    if (edit_finished)
      refresh_derivation();
  }

  if (ImGui::Button("Save"))
  {
    if (models::try_write_hitbox_rig_file(rig_path.c_str(), rig))
      printf("[hitbox] wrote '%s'\n", rig_path.c_str());
  }
  ImGui::SameLine();
  if (ImGui::Button("Reload"))
    load_rig();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", rig_path.c_str());
}

void Animation_Tool::draw_clip_panel()
{
  const char *preview = (selected_clip == NO_CLIP_SELECTED)
                            ? "(no clips found)"
                            : clip_name_of(clip_paths[selected_clip]);

  if (ImGui::BeginCombo("Clip", preview))
  {
    for (size_t index = 0; index < clip_paths.size(); ++index)
    {
      const bool chosen = ((int)index == selected_clip);
      if (ImGui::Selectable(clip_name_of(clip_paths[index]), chosen))
      {
        selected_clip = (int)index;
        load_selected_clip();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SameLine();
  // Picks up clips that APPEARED since the tool was opened. It cannot re-read one
  // already loaded: load_animation caches by path and Asset_Pool has no eviction
  // -- handles are deque indices, so removing one would dangle every handle
  // already handed out. Re-exporting a clip you are watching needs a restart
  // until assets get a real reload path. Labelled for what it does, because a
  // button called Reload that silently showed you the old file is the exact
  // failure this tool exists to catch.
  if (ImGui::Button("Rescan directory"))
    scan_clips();

  const assets::animation_clip_t *clip = assets::get(clip_handle);
  if (!clip)
  {
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No clip loaded -- see the console");
    return;
  }

  const uint32_t frame_count = clip->frame_count();
  const float    duration    = assets::clip_duration_seconds(*clip, clip_looping);

  // A single-frame clip is every authored aim pose, and it is legitimate to look
  // at one here -- it just has no transport, because phase means nothing to it.
  if (frame_count < 2)
  {
    ImGui::Text("%s -- 1 frame, nothing to play", clip->name.c_str());
    return;
  }

  if (ImGui::Button(clip_playing ? "Pause" : "Play"))
  {
    // Pressing Play on a finished one-shot restarts it rather than doing
    // nothing, which is what the button appears to promise.
    if (!clip_playing && !clip_looping && clip_phase >= 1.0f)
      clip_phase = 0.0f;
    clip_playing = !clip_playing;
  }
  ImGui::SameLine();
  if (ImGui::Button("Rewind"))
  {
    clip_phase   = 0.0f;
    clip_playing = false;
  }
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &clip_looping);

  ImGui::SliderFloat("Speed", &clip_playback_speed, -2.0f, 4.0f, "%.2fx");

  // Scrubbing IS pausing: a slider that fought the clock for the same value
  // would snap back the instant you let go.
  if (ImGui::SliderFloat("Phase", &clip_phase, 0.0f, 1.0f, "%.3f"))
    clip_playing = false;

  // Where the sampler actually is, in the clip's own terms. Two adjacent frames
  // and a blend is the whole of what it does, so showing the fractional frame is
  // showing the interpolation rather than describing it.
  const float wrapped = clip_looping ? clip_phase - std::floor(clip_phase) : clip_phase;
  const float frame_position =
      clip_looping ? wrapped * (float)frame_count : wrapped * (float)(frame_count - 1);

  ImGui::Text("frame %.2f / %u   %.2f s / %.2f s   @ %.4g fps", frame_position, frame_count,
              wrapped * duration, duration, clip->fps);
  ImGui::Text("%s -- %u bones%s", clip->name.c_str(), clip->bone_count,
              clip->stride_distance > 0.0f ? "  (locomotion)" : "");
}

void Animation_Tool::on_draw_ui(editor_context_t &ctx)
{
  ImGui::Begin("Animation");

  // Everything below dereferences `skeleton`.
  if (!skeleton)
  {
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No model -- see the console");
    ImGui::End();
    return;
  }

  ImGui::Text("%s -- %zu bones", skeleton->name.c_str(), skeleton->bones.size());
  ImGui::Separator();

  ImGui::Text("Pose");
  int source = (int)pose_source;
  // Three names for one pose here, deliberately: "bind" is what the code
  // derives (from inverse_bind), "rest" is what Blender calls it, and "T-pose"
  // is what it looks like. They coincide because the exporter resets to rest
  // before computing inverse_bind -- confirmed 2026-08-10, when a hand-authored
  // t_pose action exported byte-identical to rest and the exporter refused it.
  // Stacked rather than SameLine'd: the first label is too wide for three
  // across, and this is a mode switch you read down, not a toolbar.
  ImGui::RadioButton("Bind (rest, a.k.a T-Pose)", &source, (int)Pose_Source::Bind);
  ImGui::RadioButton("Single authored pose", &source, (int)Pose_Source::Single_Pose);
  ImGui::RadioButton("Aim blend_weights", &source, (int)Pose_Source::Aim_Blend);
  ImGui::RadioButton("Clip playback", &source, (int)Pose_Source::Clip);
  pose_source = (Pose_Source)source;

  if (pose_source == Pose_Source::Single_Pose)
  {
    if (ImGui::BeginCombo("Authored pose", to_string(single_pose)))
    {
      for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
      {
        const entities::Aim_Pose candidate = (entities::Aim_Pose)index;
        if (ImGui::Selectable(to_string(candidate), candidate == single_pose))
          single_pose = candidate;
      }
      ImGui::EndCombo();
    }
  }

  if (pose_source == Pose_Source::Clip)
    draw_clip_panel();

  if (pose_source == Pose_Source::Aim_Blend)
  {
    const cvars::cvar_state_t &cvars = *state_manager::get_client_context().cvars;
    ImGui::SliderFloat("Pitch", &pitch_degrees, -cvars.sv_aim_max_pitch, cvars.sv_aim_max_pitch);
    // Deviation, not absolute yaw: it is the twist between the feet and the
    // view, which is what the left/right poses were authored against.
    ImGui::SliderFloat("Yaw deviation", &yaw_deviation_degrees, -cvars.sv_aim_max_yaw,
                       cvars.sv_aim_max_yaw);
    if (ImGui::Button("Centre"))
    {
      pitch_degrees         = 0.0f;
      yaw_deviation_degrees = 0.0f;
    }

    const assets::aim_poses_blend_weights_t blend_weights =
        assets::compute_aim_blend(pitch_degrees, yaw_deviation_degrees, cvars.sv_aim_max_pitch,
                                  cvars.sv_aim_max_yaw);
    ImGui::Separator();
    ImGui::Text("blend_weights weights");
    for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
    {
      const entities::Aim_Pose pose = (entities::Aim_Pose)index;
      ImGui::Text("  %-9s %.3f", to_string(pose), blend_weights.weights[pose]);
    }
  }

  ImGui::Separator();
  ImGui::SliderFloat("Model yaw", &model_yaw_degrees, -180.0f, 180.0f);

  ImGui::Separator();
  ImGui::Text("Show");
  ImGui::Checkbox("Mesh", &show_mesh);
  ImGui::SameLine();
  ImGui::Checkbox("Wireframe", &wireframe);
  ImGui::SameLine();
  ImGui::BeginDisabled(wireframe); // the wireframe pipeline has no lit variant
  ImGui::Checkbox("Unlit", &unlit);
  ImGui::EndDisabled();
  ImGui::Checkbox("Skeleton", &show_skeleton);
  ImGui::SameLine();
  ImGui::Checkbox("Bone names", &show_bone_names);
  ImGui::Checkbox("Hitbox volumes", &show_hitboxes);
  ImGui::Checkbox("Movement hull", &show_movement_hull);

  ImGui::Separator();
  draw_hitbox_panel();

  ImGui::Separator();
  if (ImGui::TreeNode("Bones"))
  {
    for (uint32_t index = 0; index < (uint32_t)skeleton->bones.size(); ++index)
    {
      const linalg::vec3f head = bone_head(index);
      char                label[160];
      snprintf(label, sizeof(label), "%2u %-18s (%.1f, %.1f, %.1f)", index,
               skeleton->bones[index].name.c_str(), head.x, head.y, head.z);
      if (ImGui::Selectable(label, (int)index == selected_bone))
        selected_bone = ((int)index == selected_bone) ? -1 : (int)index;
    }
    ImGui::TreePop();
  }

  ImGui::End();
}

} // namespace client
