#include "animation_tool.hpp"
#include "../../render_assets.hpp"

#include "../../../shared/cvars/generated/cvars_generated.hpp"
#include "../../../shared/entities/generated/entities_generated.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/model_format.hpp"
#include "../../../shared/player_animator.hpp"
#include "../../../shared/player_constants.hpp"
#include "../../../shared/player_rig.hpp"
#include "../../../shared/hit_region.hpp"
#include "../../../shared/skinning.hpp"
#include "../../hitbox_debug_draw.hpp"
#include "../../renderer.hpp"
#include "../../state_manager.hpp"
#include "../entity_editor_traits.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

namespace client
{
namespace
{

constexpr assets::mesh_asset PREVIEW_MESH = assets::mesh_asset::Leet_Full;

// A leaf bone has no child to draw toward, so its segment is a stub along the
// bone's own local +Y (its height). Long enough to see which way the bone points.
constexpr float LEAF_BONE_STUB_LENGTH = 4.0f;

linalg::vec3f get_bone_head_position(const assets::posed_skeleton_t &posed, uint32_t bone_index)
{
  const linalg::vec4 &translation = posed.model_space[bone_index][3];
  return linalg::vec3f{translation.x, translation.y, translation.z};
}

// called one time to populate the tool.
assets::aim_pose_clips_t aim_clips()
{
  assets::aim_pose_clips_t clips;
  for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
  {
    const entities::Aim_Pose pose = (entities::Aim_Pose)index;
    clips[pose] = assets::get(holding_gun_aim_poses().poses[pose]);
  }
  return clips;
}

assets::aim_poses_blend_weights_t blend_weights_for_an_individual_pose(entities::Aim_Pose chosen)
{
  assets::aim_poses_blend_weights_t weights;
  for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
  {
    const entities::Aim_Pose pose = (entities::Aim_Pose)index;
    weights.weights[pose] = (pose == chosen) ? 1.0f : 0.0f;
  }
  return weights;
}

// Retried every frame on purpose: nothing here changes once it succeeds, but
// the assets can resolve late, and the frame it first succeeds is the frame the
// hitbox rig can first load.
[[nodiscard]] bool try_resolve_preview_model(preview_model_t &model, bool report)
{
  model.posed    = false;
  model.skeleton = nullptr;

  model.mesh = assets::get_mesh(PREVIEW_MESH);
  if (!model.mesh.valid())
  {
    if (report)
      log_error("[animation] preview mesh '{}' did not resolve through the asset manifest",
                assets::to_string(PREVIEW_MESH));
    return false;
  }

  const assets::mesh_asset_t* mesh = assets::get(model.mesh);
  if (!mesh || !mesh->is_skinned())
  {
    if (report)
      log_error("[animation] preview mesh '{}' has no skin arrays: it exported unskinned",
                assets::to_string(PREVIEW_MESH));
    return false;
  }

  model.skeleton = assets::get(mesh->skeleton);
  if (!model.skeleton)
  {
    if (report)
      log_error("[animation] preview mesh '{}' names a skeleton that is not in the cache",
                assets::to_string(PREVIEW_MESH));
    return false;
  }

  return true;
}

[[nodiscard]] bool try_update_pose(preview_model_t &model, const pose_controls_t &controls,
                                   const clip_playback_t &clip, const aim_settings_t &settings,
                                   bool report)
{
  switch (controls.pose_source)
  {
    case pose_source_t::Bind:
    {
      assets::compute_bind_pose(*model.skeleton, model.sampled_pose);
      break;
    }
      
    // single pose is just a blend with 1 hot.
    case pose_source_t::Single_Pose:
    case pose_source_t::Aim_Blend:
    {
      const assets::aim_poses_blend_weights_t weights =
          controls.pose_source == pose_source_t::Single_Pose
              ? blend_weights_for_an_individual_pose(controls.single_pose)
              : assets::compute_aim_blend(controls.pitch_degrees, controls.yaw_deviation_degrees,
                                          settings.max_pitch_degrees, settings.max_yaw_degrees);

      assets::sample_aim_pose(model.sampled_pose, aim_clips(), weights);
      break;
    }

    case pose_source_t::Clip:
    {
      const assets::animation_asset_t* asset = assets::get(clip.handle);
      if (clip.selected == assets::animation_asset::Missing || !asset)
      {
        if (report)
          log_error("[animation] no clip is selected; pick one in the Clip panel");
        return false;
      }

      assets::sample_animation_clip_at(model.sampled_pose, *asset, clip.phase, clip.looping);
      break;
    }
  }

  const uint32_t bone_count = (uint32_t)model.skeleton->bones.size();
  if (model.sampled_pose.parent_space.size() != bone_count)
  {
    if (report)
      log_error("[animation] the sampled pose has {} bones but skeleton '{}' has {}",
                model.sampled_pose.parent_space.size(), model.skeleton->name, bone_count);
    return false;
  }

  assets::compute_posed_skeleton(*model.skeleton, model.sampled_pose, model.posed_skeleton);

  model.posed = true;
  return true;
}

void select_clip(clip_playback_t &clip, assets::animation_asset id)
{
  clip.selected = id;
  clip.handle   = assets::get_animation(id);
  clip.phase    = 0.0f;
}

assets::animation_asset first_clip()
{
  return assets::animation_asset_COUNT > 1 ? (assets::animation_asset)1
                                           : assets::animation_asset::Missing;
}

void advance_clip(clip_playback_t &clip, const assets::animation_asset_t &asset, float dt)
{
  if (!clip.playing)
    return;
  
  // a clip consisting of one pose has _no_ duration. so nothing to advance.
  const float duration = assets::clip_duration_seconds(asset, clip.looping);
  if (!(duration > 0.0f)) return;

  clip.phase += dt * clip.playback_speed / duration;

  if (clip.looping)
  {
    clip.phase -= std::floor(clip.phase);
    return;
  }
  
  // oneshot playback ends on the final pose.
  if (clip.phase >= 1.0f)
  {
    clip.phase   = 1.0f;
    clip.playing = false;
  }

  else if (clip.phase < 0.0f) // reachable with a negative speed
  {
    clip.phase   = 0.0f;
    clip.playing = false;
  }
}

// if there's a hitbox rig from a file, get that file path. otherwise, get the path it _should_ be to write to.
std::string rig_path_of(const hitbox_workspace_t &workspace, const assets::skeleton_t &skeleton)
{
  if (workspace.file_based_hitbox_rig)
    return assets::hitbox_rig_manifest()[(size_t)*workspace.file_based_hitbox_rig].path;

  const std::filesystem::path mesh_path = assets::mesh_asset_manifest()[(size_t)PREVIEW_MESH].path;
  return (mesh_path.parent_path() / (skeleton.name + ".hitboxes")).generic_string();
}

void recompute_vertex_analysis(hitbox_workspace_t &workspace, const assets::mesh_asset_t &mesh,
                               const assets::skeleton_t &skeleton)
{
  if (workspace.rig.volumes.empty())
    return;

  workspace.guesstimated_hitboxes_from_bones.resize(workspace.rig.volumes.size());
  assets::guesstimate_hitbox_sizes(mesh, skeleton, workspace.rig, workspace.guesstimated_hitboxes_from_bones);

  // Coverage is a BIND-pose property (skinning moves skin and volumes together),
  // so it is computed against the bind pose (read: t-pose) rather than the one on
  // screen. Both of these walk every vertex against every volume and derivation
  // prints a table, so this runs on edits that change the answer.
  std::vector<linalg::mat4f> bind_model(skeleton.bones.size());
  assets::compute_bind_model_matrices(skeleton, bind_model);

  std::vector<assets::posed_hitbox_t> bind_hitboxes(workspace.rig.volumes.size());
  assets::compute_posed_hitboxes(workspace.rig, bind_model, bind_hitboxes);
  workspace.coverage = assets::compute_hitbox_coverage(mesh, skeleton, bind_hitboxes,
                                                       assets::HITBOX_COVERAGE_TOLERANCE);
}

void load_rig(hitbox_workspace_t &workspace, const preview_model_t &model)
{
  workspace = {};
  workspace.loaded_for_skeleton = model.skeleton;

  if (!model.skeleton)
  {
    log_error("[hitbox] no skeleton is resolved, so there is nothing to resolve bone names against");
    return;
  }

  workspace.file_based_hitbox_rig = assets::try_from_string<assets::hitbox_rig>(model.skeleton->name);
  if (!workspace.file_based_hitbox_rig)
    return;


  workspace.rig = *assets::get(assets::get_hitbox_rig(*workspace.file_based_hitbox_rig));

  if (workspace.rig.skeleton != model.skeleton)
  {
    log_error("[hitbox] rig '{}' resolved against skeleton '{}', but the preview mesh is skinned "
              "by '{}'; the volumes would sit on the wrong bones",
              workspace.rig.name, workspace.rig.skeleton_name, model.skeleton->name);
    workspace.rig = {};
    return;
  }

  // the volumes here are hitbox volumes.
  workspace.posed_hitboxes.resize(workspace.rig.volumes.size());

  if (const assets::mesh_asset_t *mesh = assets::get(model.mesh))
    recompute_vertex_analysis(workspace, *mesh, *model.skeleton);
}

// The volumes follow the pose every frame, through the same model-space matrices
// the mesh is skinned by -- which is the whole claim phase B makes.
void pose_hitboxes(hitbox_workspace_t &workspace, const assets::posed_skeleton_t& posed_skeleton)
{
  if (workspace.posed_hitboxes.empty())
  {
    log_warning("no hitboxes to pose.");
    return;
  }

  assets::compute_posed_hitboxes(workspace.rig, posed_skeleton.model_space, workspace.posed_hitboxes);
  workspace.excursion = assets::compute_hull_excursion(workspace.posed_hitboxes, shared::player_half_width,
                                                       shared::player_half_height * 2.0f);
}

} // namespace

void Animation_Tool::on_enable(editor_context_t& ctx)
{
  display.selected_bone_index = display_options_t::NO_BONE_SELECTED;
  workspace.selected_volume_index = hitbox_workspace_t::NO_HITBOX_VOLUME_SELECTED;

  aim_settings = aim_settings_from(*state_manager::get_client_context().cvars);

  // this is by definition true only on the very first entry.
  if (clip.selected == assets::animation_asset::Missing)
    select_clip(clip, first_clip());


  refresh_preview();
}

// Resolve, then load the rig if the skeleton changed under us, then pose. The
// rig hangs off the SKELETON and not off the pose: it is the resolve that has
// to have succeeded before it can load.
void Animation_Tool::refresh_preview()
{
  const bool report_failures = !model.failure_logged;
  model.failure_logged       = true;

  if (!try_resolve_preview_model(model, report_failures))
    return;

  if (workspace.loaded_for_skeleton != model.skeleton)
    load_rig(workspace, model);

  if (!try_update_pose(model, pose_controls, clip, aim_settings, report_failures))
    return;

  model.failure_logged = false;
}

void Animation_Tool::on_disable(editor_context_t& ctx) {}

void Animation_Tool::on_update(editor_context_t& ctx, const viewport_state_t& view, float dt)
{
  aim_settings = aim_settings_from(*state_manager::get_client_context().cvars);

  //advance with _this_ frame's dt.
  if (pose_controls.pose_source == pose_source_t::Clip)
    if (const assets::animation_asset_t *asset = assets::get(clip.handle))
      advance_clip(clip, *asset, dt);

  refresh_preview();
  if (!model.posed)
    return;

  pose_hitboxes(workspace, model.posed_skeleton);
}

void Animation_Tool::on_mouse_down(editor_context_t& ctx, const input::mouse_event_t &e) {}
void Animation_Tool::on_mouse_drag(editor_context_t& ctx, const input::mouse_event_t &e) {}
void Animation_Tool::on_mouse_up(editor_context_t& ctx, const input::mouse_event_t &e) {}
void Animation_Tool::on_key_down(editor_context_t& ctx, const key_event_t &e) {}

std::optional<view_focus_t> Animation_Tool::view_focus() const
{

  // if there's nothing just take the canonical player height as a signal.
  if (!model.posed || model.posed_skeleton.model_space.empty())
    return view_focus_t{.center = {0.0f, shared::player_half_height, 0.0f},
                        .radius = shared::player_half_height};


  linalg::vec3f minimum = get_bone_head_position(model.posed_skeleton, 0);
  linalg::vec3f maximum = minimum;
  for (uint32_t index = 1; index < (uint32_t)model.posed_skeleton.model_space.size(); ++index)
  {
    const linalg::vec3f head = get_bone_head_position(model.posed_skeleton, index);
    minimum = {std::min(minimum.x, head.x), std::min(minimum.y, head.y),
               std::min(minimum.z, head.z)};
    maximum = {std::max(maximum.x, head.x), std::max(maximum.y, head.y),
               std::max(maximum.z, head.z)};
  }

  const linalg::vec3f center = (minimum + maximum) * 0.5f;
  const linalg::vec3f extent = (maximum - minimum) * 0.5f;

  // vertices extend past the bones, but no sense recomputing the bounds. just take
  // a margin.
  constexpr float SKIN_MARGIN = 8.0f;
  const float radius = std::max({extent.x, extent.y, extent.z, shared::player_half_height}) + SKIN_MARGIN;
  return view_focus_t{.center = center, .radius = radius};
}

void Animation_Tool::on_draw_overlay(editor_context_t& ctx, pass_builder_t &draws)
{
  // the model is placed at the origin, but the orientation needs to be correct.
  const shared::model_to_world_t to_world =
      shared::model_to_world(display.model_yaw_degrees, {0, 0, 0});

  if (model.posed && display.mesh)
  {
    const renderer::mesh_handle_t mesh = get_render_mesh(model.mesh);

    renderer::mesh_draw_t draw{};
    draw.mesh      = mesh;
    draw.transform = linalg::compose_transform(
        {0, 0, 0}, linalg::from_axis_angle({0.f, 1.f, 0.f}, display.model_yaw_degrees),
        {1, 1, 1});
    // `posed_skeleton` is a member, so the Span outlives this call and stays
    // valid until render_frame reads it.
    draw.pose = model.posed_skeleton.skinning;
    draw.fill = display.wireframe ? renderer::fill_mode_t::wireframe
                                  : renderer::fill_mode_t::solid;
    if (display.unlit)
      draw.material_overrides = material_variant(mesh, {.shader = renderer::shader_t::unlit});
    draws.meshes.push_back(draw);
  }

  if (model.posed && display.skeleton)
  {
    const uint32_t bone_count = (uint32_t)model.skeleton->bones.size();

    // draw bone lines
    std::vector<bool> has_child(bone_count, false);
    for (uint32_t index = 0; index < bone_count; ++index)
    {
      const int32_t parent = model.skeleton->bones[index].parent_index;
      if (parent != assets::ROOT_BONE_INDEX)
      {
        has_child[parent] = true;
        const color_t color = ((int)parent == display.selected_bone_index ||
                               (int)index == display.selected_bone_index)
                                  ? colors::gold
                                  : colors::green;
        draws.debug.line(to_world.point(get_bone_head_position(model.posed_skeleton, parent)),
                         to_world.point(get_bone_head_position(model.posed_skeleton, index)), color);
      }
    }

    // draw bone heads.
    for (uint32_t index = 0; index < bone_count; ++index)
    {
      const linalg::vec3f local    = get_bone_head_position(model.posed_skeleton, index);
      const linalg::vec3f head     = to_world.point(local);
      const bool          selected = (int)index == display.selected_bone_index;

      if (!has_child[index])
      {
        // leaf bone: use the stub length to still draw a line.
        const linalg::vec3f axis = to_world.point(
            local + assets::bone_direction(model.posed_skeleton.model_space[index]) *
                        LEAF_BONE_STUB_LENGTH);
        draws.debug.line(head, axis, selected ? colors::gold : colors::green);
      }

      // Joints get a marker so a bone that has collapsed onto its parent -- the
      // classic wrong-inverse-bind symptom -- is visible as a doubled dot
      // rather than as nothing at all.
      draws.debug.box(head, {0.6f, 0.6f, 0.6f}, selected ? colors::gold : colors::white);

      if (display.bone_names || selected)
        draws.debug.text(head, model.skeleton->bones[index].name.c_str(), colors::white);
    }
  }

  // draw the hitbox volumes.
  if (model.posed && display.hitboxes)
  {

    const auto face = [&](Span<const linalg::vec3f> polygon, color_t color)
    {
      draws.debug.filled_polygon(polygon, color, 0.f, {.draw_when_occluded = true});
    };

    const auto line = [&](const linalg::vec3f& start, const linalg::vec3f& end, color_t color)
    { draws.debug.line(start, end, color, 0.f, 0.f, /*draw_when_occluded*/ true); };

    for (uint32_t index = 0; index < (uint32_t)workspace.posed_hitboxes.size(); ++index)
    {
      // if this hitbox is selected, do the oscillation pink->white, otherwise, take the section color based on region.
      const color_t color = (int)index == workspace.selected_volume_index
                                ? compute_selection_pulse_color(ctx.time)
                                : hit_region_color(workspace.posed_hitboxes[index].region);

      assets::posed_hitbox_t placed = workspace.posed_hitboxes[index];
      shared::place_hitbox(placed, to_world);

      draw_posed_hitbox_faces(face, placed, with_alpha(color, HITBOX_FACE_ALPHA));
      draw_posed_hitbox(line, placed, color);

      // also draw the volume name for the selected hitbox.
      if ((int)index == workspace.selected_volume_index)
        draws.debug.text(placed.start, workspace.rig.volumes[index].volume.name.c_str(),
                         colors::white);
    }
  }

  // the hull that player_move uses. not authorable here, but just for reference.
  if (display.movement_hull)
  {
    draws.debug.box({0, shared::player_half_height, 0},
                    {shared::player_half_width, shared::player_half_height,
                     shared::player_half_width},
                    colors::white);
  }
}

namespace
{

// The sliders floor at 0.1, so a non-positive size is one nobody ever authored.
void use_guesstimated_size_if_unauthored(assets::hitbox_volume_t &volume,
                                    const assets::guesstimated_hitbox_from_bone_t &guesstimated)
{
  if (assets::hitbox_shape_uses_radius(volume.shape))
  {
    if (volume.radius <= 0.0f)
      volume.radius = guesstimated.radius;
    return;
  }

  if (volume.half_extents.x <= 0.0f || volume.half_extents.y <= 0.0f ||
      volume.half_extents.z <= 0.0f)
    volume.half_extents = guesstimated.half_extents;
}

[[nodiscard]] bool draw_hitbox_volume_inspector(assets::rigged_hitbox_volume_t &rigged,
                                         const assets::guesstimated_hitbox_from_bone_t &guesstimated)
{
  assets::hitbox_volume_t &hitbox_volume = rigged.volume;
  bool                     edit_finished = false;

  ImGui::SetNextItemWidth(120.0f);
  if (ImGui::BeginCombo("shape", assets::to_string(hitbox_volume.shape)))
  {
    for (uint32_t index = 0; index < (uint32_t)assets::hitbox_shape_t::Count; ++index)
    {
      const assets::hitbox_shape_t candidate = (assets::hitbox_shape_t)index;
      if (!ImGui::Selectable(assets::to_string(candidate), candidate == hitbox_volume.shape))
        continue;

      hitbox_volume.shape = candidate;
      if (candidate == assets::hitbox_shape_t::Sphere)
      {
        hitbox_volume.end_bone   = hitbox_volume.start_bone;
        rigged.end_bone   = rigged.start_bone;
        rigged.span_bones = {rigged.start_bone};
      }

      use_guesstimated_size_if_unauthored(hitbox_volume, guesstimated);

      edit_finished = true;
    }
    ImGui::EndCombo();
  }

  if (assets::hitbox_shape_uses_radius(hitbox_volume.shape))
  {
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("radius", &hitbox_volume.radius, 0.05f, 0.1f, 64.0f, "%.2f");
    edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton("fill from guess"))
    {
      hitbox_volume.radius = guesstimated.radius;
      edit_finished = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%.2f", guesstimated.radius);
  }
  else
  {
     // no radius so we need the bone direction.
    ImGui::SetNextItemWidth(220.0f);
    ImGui::DragFloat3("half-extents (right, up, along)", &hitbox_volume.half_extents.x, 0.05f, 0.1f,
                      64.0f, "%.2f");
    edit_finished |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton("fill from guess"))
    {
      hitbox_volume.half_extents = guesstimated.half_extents;
      edit_finished       = true;
    }
    ImGui::TextDisabled("guess %.2f %.2f %.2f", guesstimated.half_extents.x,
                        guesstimated.half_extents.y, guesstimated.half_extents.z);
  }

  // Bone space, so it rotates with the pose. Only the head and the hands want
  // one, which is why it is here rather than in the table.
  ImGui::SetNextItemWidth(120.0f);
  ImGui::DragFloat("offset along start bone", &hitbox_volume.offset, 0.05f, -32.0f, 32.0f, "%.2f");
  edit_finished |= ImGui::IsItemDeactivatedAfterEdit();

  return edit_finished;
}

// list all hitboxes and allow modification. changes in the file can still reflect.
void draw_hitbox_volume_table(hitbox_workspace_t &workspace)
{
  if (!ImGui::BeginTable("Hitbox Volumes", 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
    return;

  ImGui::TableSetupColumn("volume");
  ImGui::TableSetupColumn("shape");
  ImGui::TableSetupColumn("span");
  ImGui::TableSetupColumn("region");
  ImGui::TableSetupColumn("size");
  ImGui::TableSetupColumn("guess");
  ImGui::TableHeadersRow();

  for (uint32_t index = 0; index < (uint32_t)workspace.rig.volumes.size(); ++index)
  {
    const assets::hitbox_volume_t &volume = workspace.rig.volumes[index].volume;
    const bool                     round  = assets::hitbox_shape_uses_radius(volume.shape);
    ImGui::PushID((int)index);
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    if (ImGui::Selectable(volume.name.c_str(), (int)index == workspace.selected_volume_index,
                          ImGuiSelectableFlags_SpanAllColumns))
      workspace.selected_volume_index = ((int)index == workspace.selected_volume_index)
                                            ? hitbox_workspace_t::NO_HITBOX_VOLUME_SELECTED
                                            : (int)index;

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
    if (index < workspace.guesstimated_hitboxes_from_bones.size())
    {
      if (round)
        ImGui::Text("%.2f", workspace.guesstimated_hitboxes_from_bones[index].radius);
      else
        ImGui::Text("%.2f %.2f %.2f", workspace.guesstimated_hitboxes_from_bones[index].half_extents.x,
                    workspace.guesstimated_hitboxes_from_bones[index].half_extents.y, workspace.guesstimated_hitboxes_from_bones[index].half_extents.z);
    }
    else
      ImGui::TextDisabled("--");

    ImGui::PopID();
  }
  ImGui::EndTable();
}

void draw_hitbox_panel(hitbox_workspace_t &workspace, const preview_model_t &model)
{
  ImGui::Text("Hit volumes");

  const assets::mesh_asset_t *mesh = assets::get(model.mesh);
  const std::string           path = rig_path_of(workspace, *model.skeleton);

  if (workspace.rig.volumes.empty())
  {
    ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "%s", "no volumes attached to this rig.");
    ImGui::TextDisabled("%s", path.c_str());

    if (mesh && ImGui::Button("Write template"))
    {
      const std::string template_path = path + ".template";
      const assets::hitbox_rig_t template_rig =
          assets::make_hitbox_rig_template(*mesh, *model.skeleton);
      if (models::try_write_hitbox_rig_file(template_path.c_str(), template_rig))
        log_terminal("[hitbox] wrote a template to '{}'. write whatever and change the suffix to .hitboxes",
                     template_path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
      load_rig(workspace, model);
    return;
  }

  const char* volume_with_the_most_excursion =
      workspace.excursion.volume_index < 0
          ? "none"
          : workspace.rig.volumes[(size_t)workspace.excursion.volume_index].volume.name.c_str();
  const bool over_budget = workspace.excursion.distance > assets::HITBOX_MAX_HULL_EXCURSION;
  ImGui::TextColored(over_budget ? ImVec4(1, 0.5f, 0.3f, 1) : ImVec4(0.6f, 0.9f, 0.6f, 1),
                     "hull excursion %.2f / %.1f  (%s)", workspace.excursion.distance,
                     assets::HITBOX_MAX_HULL_EXCURSION, volume_with_the_most_excursion);

  if (workspace.coverage.vertex_count > 0)
  {
    const char* worst_bone = workspace.coverage.worst_bone < 0 ? "none" : model.skeleton->bones[(size_t)workspace.coverage.worst_bone].name.c_str();
    ImGui::Text("coverage: %u/%u vertices outside every volume, worst %.2f (%s)",
                workspace.coverage.uncovered_vertex_count, workspace.coverage.vertex_count,
                workspace.coverage.worst_distance, worst_bone);
  }

  draw_hitbox_volume_table(workspace);

  if (workspace.selected_volume_index != hitbox_workspace_t::NO_HITBOX_VOLUME_SELECTED)
  {
    const size_t selected = (size_t)workspace.selected_volume_index;
    const assets::guesstimated_hitbox_from_bone_t guesstimated =
        selected < workspace.guesstimated_hitboxes_from_bones.size() ? workspace.guesstimated_hitboxes_from_bones[selected] : assets::guesstimated_hitbox_from_bone_t{};

    if (draw_hitbox_volume_inspector(workspace.rig.volumes[selected], guesstimated) && mesh)
      recompute_vertex_analysis(workspace, *mesh, *model.skeleton);
  }

  if (ImGui::Button("Save"))
  {
    if (models::try_write_hitbox_rig_file(path.c_str(), workspace.rig))
      log_terminal("[hitbox] wrote '{}'", path);
  }
  ImGui::SameLine();
  if (ImGui::Button("Reload"))
    load_rig(workspace, model);
  ImGui::SameLine();
  ImGui::TextDisabled("%s", path.c_str());
}

void draw_animation_clip_panel(clip_playback_t &clip)
{
  // Every `.animation` the manifest carries. There is no directory scan and no
  // rescan button: a clip that is not in the manifest is a clip this build does
  // not have, and adding one is a rebuild either way.
  const char* preview = clip.selected == assets::animation_asset::Missing
                            ? "(no clips in the manifest)"
                            : assets::to_string(clip.selected);

  if (ImGui::BeginCombo("Animation Clip", preview))
  {
    for (uint32_t index = 1; index < assets::animation_asset_COUNT; ++index)
    {
      const assets::animation_asset candidate = (assets::animation_asset)index;
      if (ImGui::Selectable(assets::to_string(candidate), candidate == clip.selected))
        select_clip(clip, candidate);
    }
    ImGui::EndCombo();
  }

  const assets::animation_asset_t* asset = assets::get(clip.handle);
  if (clip.selected == assets::animation_asset::Missing || !asset)
  {
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No clip selected");
    return;
  }

  const uint32_t frame_count = asset->frame_count();
  const float duration = assets::clip_duration_seconds(*asset, clip.looping);

  if (frame_count < 2)
  {
    ImGui::Text("%s 1 frame, nothing to play", asset->name.c_str());
    return;
  }

  if (ImGui::Button(clip.playing ? "Pause" : "Play"))
  {
    // pressing play on a oneshot just plays it again.
    if (!clip.playing && !clip.looping && clip.phase >= 1.0f)
      clip.phase = 0.0f;
    clip.playing = !clip.playing;
  }
  ImGui::SameLine();
  if (ImGui::Button("Rewind"))
  {
    clip.phase   = 0.0f;
    clip.playing = false;
  }
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &clip.looping);

  ImGui::SliderFloat("Speed", &clip.playback_speed, -2.0f, 4.0f, "%.2fx");

  // to properly support scrubbing, we should stop playing because they interfere.
  if (ImGui::SliderFloat("Phase", &clip.phase, 0.0f, 1.0f, "%.3f"))
    clip.playing = false;

  // Where the sampler actually is, in the clip's own terms. Two adjacent frames
  // and a blend is the whole of what it does, so showing the fractional frame is
  // showing the interpolation rather than describing it.
  const float wrapped = clip.looping ? clip.phase - std::floor(clip.phase) : clip.phase;
  const float frame_position =
      clip.looping ? wrapped * (float)frame_count : wrapped * (float)(frame_count - 1);

  ImGui::Text("frame %.2f / %u   %.2f s / %.2f s   @ %.4g fps", frame_position, frame_count,
              wrapped * duration, duration, asset->fps);
  ImGui::Text("%s :  %u bones%s", asset->name.c_str(), asset->bone_count,
              asset->stride_distance > 0.0f ? "  (locomotion)" : "");
}

} // namespace

void Animation_Tool::on_draw_ui(editor_context_t& ctx)
{
  ImGui::Begin("Animation");
  // no bone is placed , return, because the rest of this function assumes there is one.
  if (!model.posed)
  {
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No model to pose.");
    ImGui::End();
    return;
  }

  ImGui::Text("%s : %zu bones", model.skeleton->name.c_str(), model.skeleton->bones.size());
  ImGui::Separator();

  ImGui::Text("Pose");
  int source = (int)pose_controls.pose_source;

  ImGui::RadioButton("Bind (rest, a.k.a T-Pose)", &source, (int)pose_source_t::Bind);
  ImGui::RadioButton("Single authored pose", &source, (int)pose_source_t::Single_Pose);
  ImGui::RadioButton("Aim blend", &source, (int)pose_source_t::Aim_Blend);
  ImGui::RadioButton("Clip playback", &source, (int)pose_source_t::Clip);
  pose_controls.pose_source = (pose_source_t)source;

  if (pose_controls.pose_source == pose_source_t::Single_Pose)
  {
    if (ImGui::BeginCombo("Authored pose", to_string(pose_controls.single_pose)))
    {
      for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
      {
        const entities::Aim_Pose candidate = (entities::Aim_Pose)index;
        if (ImGui::Selectable(to_string(candidate), candidate == pose_controls.single_pose))
          pose_controls.single_pose = candidate;
      }
      ImGui::EndCombo();
    }
  }

  if (pose_controls.pose_source == pose_source_t::Clip)
    draw_animation_clip_panel(clip);

  if (pose_controls.pose_source == pose_source_t::Aim_Blend)
  {
    ImGui::SliderFloat("Pitch", &pose_controls.pitch_degrees, -aim_settings.max_pitch_degrees,
                       aim_settings.max_pitch_degrees);
    // Deviation, not absolute yaw: it is the twist between the feet and the
    // view, which is what the left/right poses were authored against.
    ImGui::SliderFloat("Yaw deviation", &pose_controls.yaw_deviation_degrees,
                       -aim_settings.max_yaw_degrees, aim_settings.max_yaw_degrees);
    if (ImGui::Button("Centre"))
    {
      pose_controls.pitch_degrees         = 0.0f;
      pose_controls.yaw_deviation_degrees = 0.0f;
    }

    const assets::aim_poses_blend_weights_t blend_weights = assets::compute_aim_blend(
        pose_controls.pitch_degrees, pose_controls.yaw_deviation_degrees,
        aim_settings.max_pitch_degrees, aim_settings.max_yaw_degrees);
    ImGui::Separator();
    ImGui::Text("Blend weights");
    for (uint32_t index = 0; index < entities::Aim_Pose_COUNT; ++index)
    {
      const entities::Aim_Pose pose = (entities::Aim_Pose)index;
      ImGui::Text("  %-9s %.3f", to_string(pose), blend_weights.weights[pose]);
    }
  }

  ImGui::Separator();
  ImGui::SliderFloat("Model yaw", &display.model_yaw_degrees, -180.0f, 180.0f);

  ImGui::Separator();
  ImGui::Text("Show");
  ImGui::Checkbox("Mesh", &display.mesh);
  ImGui::SameLine();
  ImGui::Checkbox("Wireframe", &display.wireframe);
  ImGui::SameLine();
  ImGui::BeginDisabled(display.wireframe); // the wireframe pipeline has no lit variant
  ImGui::Checkbox("Unlit", &display.unlit);
  ImGui::EndDisabled();
  ImGui::Checkbox("Skeleton", &display.skeleton);
  ImGui::SameLine();
  ImGui::Checkbox("Bone names", &display.bone_names);
  ImGui::Checkbox("Hitbox volumes", &display.hitboxes);
  ImGui::Checkbox("Movement hull", &display.movement_hull);

  ImGui::Separator();
  draw_hitbox_panel(workspace, model);

  ImGui::Separator();
  if (ImGui::TreeNode("Bones"))
  {
    for (uint32_t index = 0; index < (uint32_t)model.skeleton->bones.size(); ++index)
    {
      const linalg::vec3f head = get_bone_head_position(model.posed_skeleton, index);
      char                label[160];
      snprintf(label, sizeof(label), "%2u %-18s (%.1f, %.1f, %.1f)", index,
               model.skeleton->bones[index].name.c_str(), head.x, head.y, head.z);
      if (ImGui::Selectable(label, (int)index == display.selected_bone_index))
        display.selected_bone_index = ((int)index == display.selected_bone_index)
                                          ? display_options_t::NO_BONE_SELECTED
                                          : (int)index;
    }
    ImGui::TreePop();
  }

  ImGui::End();
}

} // namespace client
