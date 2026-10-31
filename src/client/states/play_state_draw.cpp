#include "../../shared/frame_timing.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "../../shared/network/entity_serialization.hpp"
#include "entities/generated/entities_generated.hpp"
#include "play_state.hpp"
#include "play_state_internal.hpp"
#include "../audio/audio_system.hpp"
#include "../console.hpp"
#include "../hud.hpp"
#include "../ghost_playback.hpp"
#include "../replay_panel.hpp"
#include "../weapon_fire_audio.hpp"
#include "../surface_audio.hpp"
#include "../held_snapshot.hpp"
#include "../event_handlers.hpp"
#include "../../shared/cvars/cvar_console.hpp"
#include "../../shared/movement/player_constants.hpp"
#include "../../shared/round_phase_rules.hpp"
#include "../../shared/hit_region.hpp"
#include "../../shared/network/subtick_codec.hpp"
#include "../../shared/subtick.hpp"
#include "../../shared/collision/disabled_geometry.hpp"
#include "../../shared/timer_fraction.hpp"
#include "../../shared/collision/fixed_arc_flight.hpp"
#include "../../shared/tween.hpp"
#include "../../shared/collision/canopy.hpp"
#include "../../shared/merge.hpp"
#include "../../shared/collision/spawned_platforms.hpp"
#include "../../shared/movement/movement_modifiers.hpp"
#include "../../shared/movement/movement_volumes.hpp"
#include "../../shared/weapon_instance.hpp"
#include "../../shared/weapons.hpp"
#include "../../shared/assets/asset.hpp"
#include "../../shared/collision/debug_collision.hpp"
#include "../../shared/timed_function.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <print>
#include <tuple>
#include "../geometry_renderer.hpp"
#include "../render_assets.hpp"
#include "../../shared/network/quantization.hpp"
#include "../../shared/network/cvar_mirror.hpp"
#include "../../shared/network/map_transfer.hpp"
#include "../drawn_entity.hpp"
#include "../entity_hitbox_overlay.hpp"
#include "../hitbox_debug_draw.hpp"
#include "../shadow_debug_draw.hpp"
#include "../blob_shadow.hpp"
#include "../spot_beam.hpp"
#include "../fly_camera.hpp"
#include "../input.hpp"
#include "../../shared/animation/player_animator.hpp"
#include "../../shared/animation/player_rig.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "imgui.h"
#include <fstream>
#include <print>

namespace client
{

namespace
{

struct ghost_tint_t
{
  entities::Team_Allegiance team;
  linalg::vec3f             tint;
};

constexpr Enum_Array<entities::Team_Allegiance, ghost_tint_t> GHOST_TINTS = {{
    {entities::Team_Allegiance::Red, {1.0f, 0.45f, 0.35f}},
    {entities::Team_Allegiance::Blu, {0.35f, 0.55f, 1.0f}},
    {entities::Team_Allegiance::Free_For_All, {0.35f, 0.8f, 1.0f}},
}};

static_assert(rows_in_enum_order<&ghost_tint_t::team>(GHOST_TINTS),
              "GHOST_TINTS rows are not in Team_Allegiance order.");

renderer::particle_emitter_parameters_t
emitter_parameters(const entities::Particle_Emitter_Entity &emitter, float delta_seconds)
{
  renderer::particle_emitter_parameters_t parameters{};
  parameters.entity_id          = emitter.entity_id;
  parameters.position           = emitter.position;
  parameters.delta_time         = delta_seconds;
  parameters.emit_rate          = emitter.emit_rate;
  parameters.max_particles      = emitter.max_particles;
  parameters.lifetime_min       = emitter.lifetime_min;
  parameters.lifetime_max       = emitter.lifetime_max;
  parameters.velocity_min       = emitter.velocity_min;
  parameters.velocity_max       = emitter.velocity_max;
  parameters.spread             = emitter.spread;
  parameters.gravity            = emitter.gravity;
  parameters.drag               = emitter.drag;
  parameters.size_start         = emitter.size_start;
  parameters.size_end           = emitter.size_end;
  parameters.rotation_speed_min = emitter.rotation_speed_min;
  parameters.rotation_speed_max = emitter.rotation_speed_max;
  parameters.color_start        = emitter.color_start;
  parameters.color_end          = emitter.color_end;
  parameters.alpha_start        = emitter.alpha_start;
  parameters.alpha_end          = emitter.alpha_end;
  return parameters;
}

renderer::particle_emitter_parameters_t
explosion_parameters(uint64_t explosion_index, const vec3f& position, float time_remaining,
                     float delta_seconds)
{
  renderer::particle_emitter_parameters_t parameters{};
  // The high bit guarantees no collision with a real entity id.
  parameters.entity_id          = 0x8000000000000000ULL | explosion_index;
  parameters.position           = position;
  parameters.delta_time         = delta_seconds;
  parameters.emit_rate          = (time_remaining > 0.6f) ? 200.0f : 0.0f;
  parameters.max_particles      = 48;
  parameters.lifetime_min       = 0.3f;
  parameters.lifetime_max       = 0.8f;
  parameters.velocity_min       = 40.0f;
  parameters.velocity_max       = 120.0f;
  parameters.spread             = 2.0f;
  parameters.gravity            = {0, -20.0f, 0};
  parameters.drag               = 1.5f;
  parameters.size_start         = 3.0f;
  parameters.size_end           = 8.0f;
  parameters.rotation_speed_min = -3.0f;
  parameters.rotation_speed_max = 3.0f;
  parameters.color_start        = {1.0f, 0.8f, 0.3f};
  parameters.color_end          = {0.4f, 0.4f, 0.4f};
  parameters.alpha_start        = 0.9f;
  parameters.alpha_end          = 0.0f;
  return parameters;
}

// The dust an afterimage dissolves into: puffed from the body's middle over the first third of its life, slow and
// drifting up, grey to nothing. The index rides above the explosions' so the two never share an emitter.
renderer::particle_emitter_parameters_t
afterimage_dust_parameters(const afterimage_t& afterimage, float lifetime_seconds, float delta_seconds)
{
  const float life = lifetime_seconds > 0.f ? afterimage.age_seconds / lifetime_seconds : 1.f;

  renderer::particle_emitter_parameters_t parameters{};
  parameters.entity_id          = 0xC000000000000000ULL | afterimage.index;
  parameters.position           = shared::player_hull_center(afterimage.feet);
  parameters.delta_time         = delta_seconds;
  parameters.emit_rate          = life < 0.35f ? 160.0f : 0.0f;
  parameters.max_particles      = 64;
  parameters.lifetime_min       = 0.4f;
  parameters.lifetime_max       = 0.9f;
  parameters.velocity_min       = 10.0f;
  parameters.velocity_max       = 35.0f;
  parameters.spread             = 3.0f;
  parameters.gravity            = {0, 12.0f, 0};
  parameters.drag               = 2.5f;
  parameters.size_start         = 4.0f;
  parameters.size_end           = 10.0f;
  parameters.rotation_speed_min = -1.0f;
  parameters.rotation_speed_max = 1.0f;
  parameters.color_start        = {0.75f, 0.72f, 0.68f};
  parameters.color_end          = {0.5f, 0.5f, 0.5f};
  parameters.alpha_start        = 0.6f;
  parameters.alpha_end          = 0.0f;
  return parameters;
}

struct flashlight_settings_t
{
  linalg::vec3 color          = {1.f, 1.f, 1.f};
  float        intensity      = 0.f;
  float        inner_fraction = 0.f;
};

// Whose lights reveal for THIS viewer: everyone's and the map's. Only-your-own is this function without its remote loop.
// Past the shader's cap the nearest to the camera are the ones drawn. A held light is lit by `held`, a placed one by its own fields.
void compute_drawn_reveal_cones(const client_context_t& ctx, const camera_t& camera,
                                bool camera_is_my_eye, const flashlight_settings_t& held,
                                std::vector<lit_reveal_cone_t>& out)
{
  const shared::Entity_System& system = ctx.world.session.entity_system;
  const shared::reveal_cone_settings_t settings = shared::reveal_cone_settings_from_cvars(*ctx.cvars);

  const entities::Player_Entity* my_player = try_find_my_player(ctx);
  shared::player_sight_t         own       = {};
  if (my_player != nullptr)
  {
    own         = shared::player_sight_from_entity(system, *my_player);
    own.toggles = ctx.prediction.state.toggles;
  }
  if (my_player != nullptr && shared::reveal_light_is_on(own))
  {
    const bool                       overhead = own.toggles.reveal_light_overhead;
    const entities::Reveal_Cone_Kind kind     = *own.wielded_reveal_kind;
    out.push_back({camera_is_my_eye
                       ? shared::compute_player_reveal_cone(camera.position, camera.yaw, camera.pitch, overhead,
                                                kind, settings)
                       : shared::compute_player_reveal_cone(shared::player_eye_position(drawn_local_feet(ctx)),
                                                my_player->view_angle_yaw,
                                                my_player->view_angle_pitch, overhead, kind, settings),
                   held.color, held.intensity});
  }

  for (const auto& [slot, remote_player] : ctx.replication.remote_players)
  {
    if (!remote_player.active || slot == ctx.connection.my_slot)
      continue;
    const entities::Player_Entity* player = try_find_player_in_slot(ctx, slot);
    if (player == nullptr)
      continue;
    const shared::player_sight_t sight = shared::player_sight_from_entity(system, *player);
    if (!shared::reveal_light_is_on(sight))
      continue;
    out.push_back({shared::compute_player_reveal_cone(shared::player_eye_position(remote_player.drawn_position),
                                                      remote_player.drawn_yaw, remote_player.drawn_pitch,
                                                      sight.toggles.reveal_light_overhead, *sight.wielded_reveal_kind,
                                                      settings),
                   held.color, held.intensity});
  }

  for (const entities::Reveal_Light_Entity& light : system.entities_of_type<entities::Reveal_Light_Entity>())
    if (light.switch_state.value)
      out.push_back({shared::compute_light_reveal_cone(light, drawn_ridden_pose(ctx, light)), light.color,
                     light.intensity});

  if (out.size() > renderer::MAX_SCENE_REVEAL_CONES)
  {
    const auto distance_to_camera = [&camera](const lit_reveal_cone_t& lit)
    { return linalg::length(lit.cone.apex - camera.position); };
    std::sort(out.begin(), out.end(),
              [&](const lit_reveal_cone_t& left, const lit_reveal_cone_t& right)
              { return distance_to_camera(left) < distance_to_camera(right); });
    out.resize(renderer::MAX_SCENE_REVEAL_CONES);
  }
}

} // namespace

// The quad is authored facing -X so from_view_angles(yaw, pitch) turns it to face the camera
// that grabbed it; local +Z is the camera's right and +Y its up, so uv (0,0) is the frame's
// top-left seen from where it was taken. Sized from the field of view to cover the view
// exactly at cl_grab_distance, so at the moment it lands nothing visibly changes.
void Play_State::place_grabbed_frames()
{
  const client_context_t& ctx = state_manager::get_client_context();
  const float distance_ahead  = std::max(ctx.cvars->cl_grab_distance, 1.0f);

  const Span<const renderer::grabbed_frame_t> live = renderer::grabbed_frames();

  std::erase_if(placed_grabs,
                [&](const placed_grab_t& placed)
                {
                  for (const renderer::grabbed_frame_t& frame : live)
                    if (frame.serial == placed.serial)
                      return false;
                  return true;
                });

  for (const renderer::grabbed_frame_t& frame : live)
  {
    bool placed_already = false;
    for (const placed_grab_t& placed : placed_grabs)
      placed_already = placed_already || placed.serial == frame.serial;
    if (placed_already)
      continue;

    if (!grab_quad_mesh.valid())
    {
      assets::mesh_asset_t quad;
      const linalg::vec3f normal = {-1.0f, 0.0f, 0.0f};
      quad.vertices = {{{0.0f, 0.5f, -0.5f}, normal, {0.0f, 0.0f}},
                       {{0.0f, 0.5f, 0.5f}, normal, {1.0f, 0.0f}},
                       {{0.0f, -0.5f, 0.5f}, normal, {1.0f, 1.0f}},
                       {{0.0f, -0.5f, -0.5f}, normal, {0.0f, 1.0f}}};
      quad.indices  = {0, 3, 2, 0, 2, 1};
      grab_quad_mesh = renderer::register_mesh(quad);
    }

    renderer::material_handle_t material;
    for (const grab_material_t& known : grab_materials)
      if (known.texture == frame.texture)
        material = known.material;
    if (!material.valid())
    {
      renderer::material_t description;
      description.pipeline_state.shader    = renderer::shader_t::unlit;
      description.pipeline_state.cull_mode = renderer::cull_mode_t::none;
      description.parameters.maps.albedo   = frame.texture;
      material                             = renderer::register_material(description);
      grab_materials.push_back(grab_material_t{frame.texture, material});
    }

    const camera_basis_t basis  = get_orientation_vectors(camera);
    const linalg::vec2   screen = renderer::screen_size();
    const float          height =
        2.0f * distance_ahead * std::tan(linalg::to_radians(camera.fov_degrees) * 0.5f);
    const float          width    = height * screen.x / std::max(screen.y, 1.0f);
    const linalg::vec3f  position = camera.position + basis.forward * distance_ahead;
    const linalg::mat4f  transform =
        linalg::compose_transform(position, linalg::from_view_angles(camera.yaw, camera.pitch),
                                  {1.0f, height, width});
    placed_grabs.push_back(placed_grab_t{frame.serial, material, transform});
  }

  for (const placed_grab_t& placed : placed_grabs)
  {
    renderer::mesh_draw_t draw;
    draw.mesh               = grab_quad_mesh;
    draw.transform          = placed.transform;
    draw.material_overrides = Span<const renderer::material_handle_t>(&placed.material, 1);
    scene.meshes.push_back(draw);
  }
}

// One frame's drawing inputs, handed from step to step down Play_State::draw
// the way play_frame_t is down update. Built once at the top; nothing below writes it.
struct draw_inputs_t
{
  // The frame's dt on the world's clock: paused or slowed with a replay, the frame's own otherwise.
  float world_delta_seconds = 0.f;
  // Connected, in our own body, through our own eye: what a held thing (the flashlight, a lens) is drawn for.
  bool camera_is_my_eye = false;
  drawn_tick_t drawn;
  // Where our own body is drawn this frame; a canopy hangs over it.
  vec3f local_feet = {};
  // The SWITCH the sweep answers, resolved through the same table, so a gate you walk through is a gate you
  // cannot see. Only the switch: a team wall is passable for one team and visible to everyone, so this is the
  // hidden set, never a team's disabled set. Built on the frame clock rather than reused from the prediction
  // build, which runs on the tick clock: a set held across the gap would draw a gate one frame behind the wall
  // you can already pass.
  shared::disabled_geometry_t hidden;
  blob_shadow_settings_t      blob_shadow;
  flashlight_settings_t       held_flashlight;
};

void Play_State::draw_player_blob_shadow(const client_context_t &ctx, const draw_inputs_t &frame, const vec3f &feet)
{
  if (ctx.cvars->cl_blob_shadow)
    draw_blob_shadow(scene, ctx.world.session.bvh, Span<const uint8_t>{frame.hidden}, feet, frame.blob_shadow);
}

void Play_State::draw_map_geometry(client_context_t &ctx, const draw_inputs_t &frame)
{
  shared::Entity_System &entity_system = ctx.world.session.entity_system;

  if (!ctx.cvars->debug_hide_geometry || ctx.cvars->debug_show_wireframe)
  {
    auto mover_matrices  = std::unordered_map<shared::entity_uid_t, linalg::mat4f>{};
    for (const entities::Mover_Entity &mover : entity_system.entities_of_type<entities::Mover_Entity>())
      mover_matrices[mover.entity_id] =
          shared::mover_model_matrix(get_rest_pose_for_mover(ctx, mover), drawn_mover_poses(ctx, mover).drawn);

    // A team wall is drawn in ITS team's colour: a ghost when it is ours to walk
    // through, solid when it is not, so it says whose it is either way. Which is
    // ours is the same rule the sweep asks -- a visible owner that does not
    // block us -- so the draw and the collision cannot disagree.
    const entities::Player_Entity*  my_player = try_find_my_player(ctx);
    const entities::Team_Allegiance my_team   = my_player != nullptr
                                                    ? my_player->team_allegiance
                                                    : entities::Team_Allegiance::Free_For_All;

    for (uint32_t index = 0; index < ctx.world.session.geometry.size(); ++index)
    {
      if (index < frame.hidden.size() && frame.hidden[index] != 0)
        continue;

      const shared::entity_uid_t owner_uid = index < ctx.world.session.owner_of.size()
                                                 ? ctx.world.session.owner_of[index]
                                                 : shared::null_entity_uid;
      const auto moved = mover_matrices.find(owner_uid);
      const shared::map_geometry_t &entry = ctx.world.session.geometry[index];

      const entities::Geometry_Owner_Entity* owner =
          entity_system.get<entities::Geometry_Owner_Entity>(owner_uid);

      std::optional<team_wall_tint_t> team_wall;
      if (owner != nullptr && owner->passable_by != entities::Team_Allegiance::Free_For_All)
        team_wall = {.color    = color_from_vec3(GHOST_TINTS[owner->passable_by].tint),
                     .passable = !shared::geometry_owner_blocks(*owner, my_team)};

      renderer::light_cut_t light_cut = renderer::light_cut_t::none;
      if (owner != nullptr && owner->erased_by_light)
        light_cut = renderer::light_cut_t::erased;
      else if (owner != nullptr && owner->revealed_by_light)
        light_cut = renderer::light_cut_t::revealed;
      else if (owner != nullptr && owner->erased_in_shadow)
        light_cut = renderer::light_cut_t::shadow_hole;
      else if (owner != nullptr && owner->solid_only_in_shadow)
        light_cut = renderer::light_cut_t::shadow_solid;
      else if (owner != nullptr && owner->revealed_by_lens)
        light_cut = renderer::get_light_cut_for_lens_filter(owner->lens_filter, owner->solid_only_when_lensed);

      const linalg::mat4f* moved_by = moved != mover_matrices.end() ? &moved->second : nullptr;
      if (!ctx.cvars->debug_hide_geometry)
        draw_geometry(scene, entry.value, entry.uid, ctx.world.session.materials,
                      ctx.world.session.lightmap, moved_by,
                      compute_drawn_clock_wipe_for_geometry(ctx, owner_uid, entry.value), team_wall, light_cut,
                      owner != nullptr && owner->casts_dynamic_shadow,
                      generated_mesh_check_t::Trusted_Since_Load);
      if (ctx.cvars->debug_show_wireframe)
        draw_geometry_wireframe(scene, entry.value, colors::white, -60.0f, moved_by);
    }
  }
}

void Play_State::draw_shadow_debug_and_volumes(client_context_t &ctx, const draw_inputs_t &frame)
{
  shared::Entity_System &entity_system = ctx.world.session.entity_system;


  scene.ripples = ctx.visuals.wall_ripples.ripples;
  scene.beam_ripples = ctx.visuals.beam_ripples.ripples;
  scene.shadow_volumes   = ctx.visuals.drawn_shadow_volumes;
  scene.shadow_occluders = ctx.visuals.drawn_shadow_occluders;
  if (ctx.cvars->cl_shadow_volume_debug)
  {
    // The counts and the (caster, light) pairs, printed when they change: the planes move every tick, so they stay out.
    const shared::shadow_volume_report_t& report = ctx.visuals.drawn_shadow_volume_report;
    std::string line = std::format(
        "shadow volumes: lights {} receivers {} casters {} cast {} kept {}{} | refused: inside caster {} open "
        "silhouette {} on silhouette {} too many planes {} outside beam {} reaching nothing {} | occluders {}{} "
        "skipped {} | caster/light:",
        report.cutting_lights, report.receiver_pieces, report.caster_pieces, report.cast, report.kept,
        report.kept > renderer::MAX_SCENE_SHADOW_VOLUMES
            ? std::format(" (ONLY {} DRAWN)", renderer::MAX_SCENE_SHADOW_VOLUMES)
            : std::string{},
        report.refused_inside_caster, report.refused_open_silhouette, report.refused_on_silhouette,
        report.refused_too_many_planes, report.refused_outside_beam,
        report.culled_reaching_nothing, ctx.visuals.drawn_shadow_occluders.size(),
        ctx.visuals.drawn_shadow_occluders.size() > renderer::MAX_SCENE_SHADOW_OCCLUDERS
            ? std::format(" (ONLY {} USED)", renderer::MAX_SCENE_SHADOW_OCCLUDERS)
            : std::string{},
        ctx.visuals.drawn_shadow_occluders_skipped);
    for (const shared::shadow_volume_t& volume : ctx.visuals.drawn_shadow_volumes)
      line += std::format(" {}/{}", volume.caster, volume.light);
    for (const shared::refused_piece_t& refused : report.refused)
    {
      line += std::format(" | REFUSED piece {} at ({:.0f} {:.0f} {:.0f}) under light {}: {}", refused.piece,
                          refused.center.x, refused.center.y, refused.center.z, refused.light,
                          shared::describe_shadow_cast_refusal(refused.reason));
      scene.debug.text(refused.center,
                       std::format("REFUSED piece {} / light {}: {}", refused.piece, refused.light,
                                   shared::describe_shadow_cast_refusal(refused.reason))
                           .c_str(),
                       colors::red);
    }
    if (line != ctx.visuals.shadow_volume_debug_line)
    {
      console::get().print_line(line);
      ctx.visuals.shadow_volume_debug_line = line;
    }

    // One colour per light, so two lights' pyramids through one caster read apart.
    constexpr Array<color_t, 6> LIGHT_COLORS = {colors::magenta, colors::cyan,   colors::yellow,
                                                colors::orange,  colors::green,  colors::hot_pink};
    std::vector<shared::entity_uid_t> lights_seen;
    for (size_t volume_index = 0; volume_index < ctx.visuals.drawn_shadow_volumes.size(); ++volume_index)
    {
      const shared::shadow_volume_t& volume = ctx.visuals.drawn_shadow_volumes[volume_index];
      auto seen = std::find(lights_seen.begin(), lights_seen.end(), volume.light);
      if (seen == lights_seen.end())
        seen = lights_seen.insert(lights_seen.end(), volume.light);
      const color_t color = LIGHT_COLORS[static_cast<uint32_t>(seen - lights_seen.begin()) % LIGHT_COLORS.size()];

      std::vector<shared::shadow_volume_edge_t> edges;
      shared::collect_shadow_volume_edges(volume, shared::SHADOW_VOLUME_DRAWN_REACH, edges);
      for (const shared::shadow_volume_edge_t& edge : edges)
        scene.debug.line(edge.a, edge.b, color, 0.f, 0.f, true);
      const std::string label = volume_index < renderer::MAX_SCENE_SHADOW_VOLUMES
                                    ? std::format("volume {}: caster {} / light {}", volume_index, volume.caster, volume.light)
                                    : std::format("NOT DRAWN: caster {} / light {}", volume.caster, volume.light);
      scene.debug.text(get_aabb_center(volume.caster_bounds), label.c_str(), color);
    }
  }
  if (ctx.cvars->cl_solid_beam_debug)
  {
    constexpr Array<color_t, 8> PIECE_COLORS = {colors::cyan,   colors::yellow, colors::magenta, colors::green,
                                                colors::orange, colors::white,  colors::hot_pink, colors::blue};
    for (const shared::mover_t& beam : ctx.visuals.drawn_solid_beams)
    {
      for (size_t index = 0; index < beam.pieces.size(); ++index)
      {
        const shared::collision_piece_t& piece = beam.pieces[index];
        const color_t                    color = PIECE_COLORS[static_cast<uint32_t>(index) % PIECE_COLORS.size()];
        for (const std::vector<linalg::vec3>& polygon : piece.face_polygons)
        {
          for (size_t corner = 0; corner < polygon.size(); ++corner)
            scene.debug.line(polygon[corner], polygon[(corner + 1) % polygon.size()], color, 0.f, 0.f, true);
          scene.debug.filled_polygon(Span<const linalg::vec3f>(polygon.data(), static_cast<uint32_t>(polygon.size())),
                                     with_alpha(color, 24), 0.f, {.rim = true});
        }
        scene.debug.text(get_aabb_center(piece.bounds), std::format("piece {}", index).c_str(), color);
      }
      scene.debug.backed_text(beam.pose_at_tick_end.position,
                              std::format("solid beam {}: {} pieces", beam.uid, beam.pieces.size()).c_str(),
                              colors::white);
    }
  }
  compute_drawn_reveal_cones(ctx, camera, frame.camera_is_my_eye, frame.held_flashlight, scene.lit_reveal_cones);
  for (const lit_reveal_cone_t& lit : scene.lit_reveal_cones)
    scene.reveal_cones.push_back(lit.cone);

  for (const entities::Fog_Volume_Entity& fog : entity_system.entities_of_type<entities::Fog_Volume_Entity>())
    add_fog_volume(scene, fog);
  shared::compute_lenses(entity_system, scene.lenses);
  scene.held_lens_channels = frame.camera_is_my_eye ? local_held_lens_channels(ctx) : 0;
  scene.lens_watchers = ctx.visuals.drawn_lens_watchers;
}

void Play_State::draw_lights_and_beams(client_context_t &ctx, const draw_inputs_t &frame)
{
  shared::Entity_System &entity_system = ctx.world.session.entity_system;


  shared::begin_frame_lights(scene.lights, ctx.world.session.lightmap);
  for (auto [entity, light] : entity_system.entities_with<entities::Light>())
  {
    (void)light;
    const shared::path_pose_t light_pose = drawn_ridden_pose(ctx, entity);
    shared::add_frame_light(scene.lights, ctx.world.session.lightmap, entity.entity_id, entity, light_pose);
    if (ctx.cvars->r_beam)
      if (const entities::Spot_Light_Entity* spot = entities::entity_as<entities::Spot_Light_Entity>(&entity);
          spot != nullptr && spot->beam && shared::light_is_switched_on(entity))
      {
        spot_beam_t beam = build_spot_beam_for_spot_light(*spot, light_pose);
        if (const std::optional<shared::beam_carve_t> carve =
                shared::try_find_beam_carve(ctx.world.session.solid_beam_cache, spot->entity_id))
          beam.carve = *carve;
        draw_spot_beam(scene, beam);
      }
  }

  for (const lit_reveal_cone_t& lit : scene.lit_reveal_cones)
    if (lit.light_intensity > 0.f)
      shared::add_dynamic_frame_light(scene.lights,
                                      build_spot_light_for_reveal_cone(lit, frame.held_flashlight.inner_fraction));
}

void Play_State::draw_rendered_entities(client_context_t &ctx, const draw_inputs_t &frame)
{
  shared::Entity_System &entity_system = ctx.world.session.entity_system;

  for (auto [entity, render] : entity_system.entities_with<entities::Render>())
  {
    // ALONGSIDE the model and ahead of every skip below: a hit volume is not an
    // alternative to the mesh, and an entity whose mesh is hidden or unresolved
    // is exactly the one whose volume you want on screen.
    if (ctx.cvars->debug_show_hitboxes)
      draw_entity_hitbox_overlay(&entity, scene);

    if (!render.visible)
      continue;

    const std::optional<drawn_entity_t> drawn =
        try_compute_drawn_entity_at_tick(ctx, entity, render, frame.drawn, frame.local_feet);
    if (!drawn)
      continue;

    const renderer::mesh_handle_t mesh = get_render_mesh(assets::get_mesh(render.mesh));
    if (!mesh.valid())
      continue;

    renderer::mesh_draw_t draw{};
    draw.mesh       = mesh;
    draw.clock_wipe = drawn->clock_wipe;
    draw.dissolve   = drawn->dissolve;
    draw.peel       = drawn->peel;
    draw.transform  = linalg::compose_transform(
        drawn->position, linalg::compose_model_rotation(drawn->orientation, render.rotation), drawn->scale);

    // Same split the geometry surface path makes, and the editor preview with
    // it: a wireframe takes neither the base colour nor the shader, because
    // there is no lit surface for either to apply to.
    if (render.is_wireframe && renderer::wireframe_supported())
    {
      draw.fill = renderer::fill_mode_t::wireframe;
    }
    else
    {
      draw.tint               = color_from_vec3(drawn->color);
      draw.material_overrides = drawn->as_ghost ? ghost_variant(mesh) : material_override(mesh, render.material);
    }
    scene.meshes.push_back(draw);
  }
}

void Play_State::draw_afterimages_and_ghosts(client_context_t &ctx, const draw_inputs_t &frame)
{


  // The bodies players were cut away from, in the aim pose each left in, dissolving. No shadow: a dissolving body's
  // whole shadow would outlast its holes.
  const float afterimage_seconds = ctx.cvars->cl_afterimage_seconds;
  for (const afterimage_t &afterimage : ctx.visuals.afterimages)
  {
    if (afterimage_seconds <= 0.f || !afterimage.render.visible)
      continue;
    const assets::asset_handle_t<assets::mesh_asset_t> mesh_asset = assets::get_mesh(afterimage.render.mesh);
    const renderer::mesh_handle_t                      mesh       = get_render_mesh(mesh_asset);
    if (!mesh.valid())
      continue;

    if (pose_count == pose_storage.size())
      pose_storage.emplace_back();
    assets::posed_skeleton_t &posed = pose_storage[pose_count++];
    posed.clear();

    const assets::mesh_asset_t *mesh_asset_data = assets::get(mesh_asset);
    if (mesh_asset_data && mesh_asset_data->is_skinned())
      if (const assets::skeleton_t *skeleton = assets::get(mesh_asset_data->skeleton))
        compute_aim_posed_skeleton(holding_gun_aim_poses(), *skeleton, afterimage.pitch, afterimage.yaw_deviation,
                                   aim_settings_from_cvars(*ctx.cvars), posed);

    renderer::mesh_draw_t draw{};
    draw.mesh      = mesh;
    draw.transform = linalg::compose_transform(
        afterimage.feet + afterimage.render.offset,
        linalg::compose_model_rotation(linalg::from_view_angles(afterimage.body_yaw, 0.f), afterimage.render.rotation),
        afterimage.render.scale);
    draw.pose          = posed.skinning;
    draw.dissolve      = std::clamp(afterimage.age_seconds / afterimage_seconds, 0.001f, 1.f);
    draw.shadow_caster = renderer::shadow_caster_t::none;
    if (ctx.cvars->cl_player_unlit)
      draw.material_overrides = material_variant(mesh, {.shader = renderer::shader_t::unlit});
    scene.meshes.push_back(draw);
  }

  if (const std::optional<double> ghost_run_tick = try_ghost_run_tick(ctx))
  {
    static const entities::Render ghost_render = entities::Player_Entity{}.render;
    const assets::asset_handle_t<assets::mesh_asset_t> mesh_asset = assets::get_mesh(ghost_render.mesh);
    const renderer::mesh_handle_t                      mesh       = get_render_mesh(mesh_asset);

    for (const shared::ghost_track_t& track : ctx.world.ghost->tracks)
    {
      const std::optional<shared::ghost_pose_t> ghost = shared::try_sample_ghost(track, *ghost_run_tick);
      if (!ghost || !mesh.valid())
        continue;

      if (pose_count == pose_storage.size())
        pose_storage.emplace_back();
      assets::posed_skeleton_t& posed = pose_storage[pose_count++];
      posed.clear();

      const assets::mesh_asset_t* mesh_asset_data = assets::get(mesh_asset);
      if (mesh_asset_data && mesh_asset_data->is_skinned())
        if (const assets::skeleton_t* skeleton = assets::get(mesh_asset_data->skeleton))
          compute_aim_posed_skeleton(holding_gun_aim_poses(), *skeleton, ghost->view_pitch,
                                     linalg::wrap_degrees(ghost->view_yaw - ghost->body_yaw),
                                     aim_settings_from_cvars(*ctx.cvars), posed);

      renderer::mesh_draw_t draw{};
      draw.mesh      = mesh;
      draw.transform = linalg::compose_transform(
          ghost->position + ghost_render.offset,
          linalg::compose_model_rotation(linalg::from_view_angles(ghost->body_yaw, 0.f), ghost_render.rotation),
          ghost_render.scale);
      draw.pose               = posed.skinning;
      draw.tint               = color_from_vec3(GHOST_TINTS[track.team].tint);
      draw.material_overrides = ghost_variant(mesh);
      draw.shadow_caster      = renderer::shadow_caster_t::none;
      scene.meshes.push_back(draw);
    }
  }
}

void Play_State::draw_remote_players(client_context_t &ctx, const draw_inputs_t &frame)
{


  // Render remote players and bots: the model, then the debug volumes.
  const std::optional<int32_t> first_person_slot = try_replay_first_person_slot(ctx.replay, *ctx.cvars);
  for (const auto &[slot, remote_player] : ctx.replication.remote_players)
  {
    if (!remote_player.active || remote_player.slot_index == ctx.connection.my_slot ||
        remote_player.slot_index == first_person_slot)
      continue;

    if (remote_player_is_hidden_by_a_merge(ctx, remote_player))
      continue;

    draw_player_blob_shadow(ctx, frame, remote_player.drawn_position);

    // The player model. A bot IS a Player_Entity, so this draws bots too --
    // which is the only way to see a third-person model without a second
    // machine. The Render component comes off the reconstructed entity rather
    // than Remote_Player_State, because that is where the snapshot put it; the
    // interpolated POSITION comes off Remote_Player_State, because that is
    // where the interpolation happens. Neither has both.
    const entities::Player_Entity* player_entity = try_find_player_in_slot(ctx, slot);
    if (player_entity != nullptr && player_entity->render.visible)
    {
      const entities::Render &render = player_entity->render;

      const assets::asset_handle_t<assets::mesh_asset_t> mesh_asset =
          assets::get_mesh(render.mesh);
      const renderer::mesh_handle_t mesh = get_render_mesh(mesh_asset);
      if (mesh.valid())
      {
        // The body is drawn at BODY_YAW, not view yaw, and the difference is
        // handed to the aim poses. Pitch never reaches the transform at all:
        // it is where the player is LOOKING, and applying it to the whole
        // model would tip them over. Distributing it across the spine is what
        // the authored pose set does (animation_def.md §5).
        //
        // The pose has to outlive this loop -- mesh_draw_t holds a Span into it
        // and nothing is recorded until render_frame -- so it goes in a slot of
        // `pose_storage`, whose element addresses a deque keeps stable as it
        // grows.
        if (pose_count == pose_storage.size())
          pose_storage.emplace_back();
        assets::posed_skeleton_t &posed = pose_storage[pose_count++];
        posed.clear();

        const assets::mesh_asset_t *mesh_asset_data = assets::get(mesh_asset);
        const assets::skeleton_t   *skeleton =
            mesh_asset_data && mesh_asset_data->is_skinned() ? assets::get(mesh_asset_data->skeleton)
                                                             : nullptr;

        if (skeleton && remote_player.death_tick != 0)
        {
          // A corpse is not aiming at anything, so the death clip REPLACES the
          // aim blend rather than layering over it -- both are full-body poses
          // and there is no crossfade in the animator yet. body_yaw below still
          // orients the body, frozen where the server stopped advancing it, so
          // the player falls in the direction they were facing.
          compute_clip_posed_skeleton(death_clip(), *skeleton,
                                      remote_player.death_animation_seconds,
                                      /*looping*/ false, posed);
        }
        else if (skeleton)
        {
          float pitch = remote_player.drawn_pitch;
          float deviation =
              linalg::wrap_degrees(remote_player.drawn_yaw - remote_player.body_yaw);

          // cl_aim_debug drives the two blend inputs directly, because nothing
          // in the game reaches them: a bot never writes a pitch, and the feet
          // chase its view yaw at cl_aim_body_turn_rate against a far slower
          // turn, so the deviation is back at 0 by the next frame. Forcing the
          // pair is the only way to sweep the pose space.
          if (ctx.cvars->cl_aim_debug)
          {
            pitch     = ctx.cvars->cl_aim_debug_pitch;
            deviation = ctx.cvars->cl_aim_debug_yaw;
          }

          compute_aim_posed_skeleton(holding_gun_aim_poses(), *skeleton, pitch, deviation,
                                     aim_settings_from_cvars(*ctx.cvars), posed);
        }

        renderer::mesh_draw_t draw{};
        draw.mesh      = mesh;
        draw.transform = linalg::compose_transform(
            remote_player.drawn_position + render.offset,
            linalg::compose_model_rotation(
                linalg::from_view_angles(remote_player.body_yaw, 0.f), render.rotation),
            render.scale);
        // An empty pose means BIND POSE, which is what an unskinned mesh should
        // look like. A pose set that failed to load cannot reach here -- that
        // death is fatal.
        draw.pose = posed.skinning;
        if (ctx.cvars->cl_player_unlit)
          draw.material_overrides = material_variant(mesh, {.shader = renderer::shader_t::unlit});
        scene.meshes.push_back(draw);
      }
      else
      {
        log_error("player slot {} mesh '{}' did not resolve — the model will be "
                  "invisible; check the asset manifest",
                  slot, assets::to_string(render.mesh));
      }
    }

    // The player origin is at the FEET -- same convention as
    // `player_eye_height` and the hitbox table -- so the hull sits entirely
    // ABOVE drawn_position rather than centered on it. Centering it was the
    // bug: half the box was underground and its top capped out at the waist,
    // which made every remote player look like they were standing in a hole.
    const vec3f rmin = {remote_player.drawn_position.x - player_half_width,
                        remote_player.drawn_position.y,
                        remote_player.drawn_position.z - player_half_width};
    const vec3f rmax = {remote_player.drawn_position.x + player_half_width,
                        remote_player.drawn_position.y + 2.f * player_half_height,
                        remote_player.drawn_position.z + player_half_width};
    // Wireframe only, and only while hitbox debugging is on: the regions below
    // live inside this hull, so a filled hull would write depth over every one
    // of them and all you would see is a green box. It hides the model for the
    // same reason, which is why cl_draw_player_hull defaults OFF now that there
    // is one to hide.
    const bool show_hitboxes = ctx.cvars->debug_show_hitboxes;
    if (ctx.cvars->cl_draw_player_hull && show_hitboxes)
      scene.debug.aabb(rmin, rmax, colors::green);

    // The volumes hitscan actually resolves against, placed by the SAME
    // function the server places them with (shared::compute_player_hitboxes)
    // off the SAME replicated inputs -- so a disagreement between what you see
    // and what you hit is visible rather than inferred. The green hull above is
    // where the player collides; these are where they get shot, and the two are
    // not the same shape.
    //
    // What this cannot show is the tick gap: the server tests these against
    // where the player was when the shot arrived, and lag compensation is still
    // to come (animation_def.md §4, guarantee 2).
    //
    // A corpse draws none, because the server tests none: the fire path skips
    // every player at health <= 0, and an overlay that kept drawing volumes
    // there would be showing you a target that does not exist.
    if (show_hitboxes && remote_player.death_tick == 0)
    {
      const shared::player_rig_t &rig = shared::player_rig();

      std::vector<assets::posed_hitbox_t> &volumes = hitbox_scratch;
      volumes.resize(rig.volume_count());
      shared::compute_player_hitboxes(rig,
                                      {.feet_position = remote_player.drawn_position,
                                       .body_yaw      = remote_player.body_yaw,
                                       .view_yaw      = remote_player.drawn_yaw,
                                       .view_pitch    = remote_player.drawn_pitch},
                                      aim_settings_from_cvars(*ctx.cvars), volumes);

      // Both halves draw when occluded, because a hit volume lives INSIDE the
      // model it belongs to -- depth-tested only, the overlay is the few slivers
      // that poke past the silhouette, which is what made it unreadable.

      // The EDGES fade out with range and the faces do not, because only one of
      // the two has a distance problem. A line is a constant width in SCREEN
      // space, so a receding wireframe keeps all of its ink while the area it
      // covers shrinks -- ten volumes' worth of rings pack into a few dozen
      // pixels and read as one solid blob. A face shrinks with the shape it
      // belongs to and stays legible the whole way out, so it takes over the
      // silhouette exactly as the edges stop being able to carry it.
      //
      // Tuned against how tall a player actually is on screen: at 90 degrees
      // and 1080p a 72-unit player is about 97px at 400 units and 28px at 1400,
      // which is where the rings stop resolving at all.
      constexpr float EDGE_FADE_START_UNITS = 400.0f;
      constexpr float EDGE_FADE_END_UNITS   = 1400.0f;

      const float view_distance =
          linalg::length(remote_player.drawn_position - camera.position);
      const float edge_fade =
          1.0f - std::clamp((view_distance - EDGE_FADE_START_UNITS) /
                                (EDGE_FADE_END_UNITS - EDGE_FADE_START_UNITS),
                            0.0f, 1.0f);

      const auto face = [&](Span<const vec3f> polygon, color_t color)
      {
        scene.debug.filled_polygon(polygon, color, 0.f, {.draw_when_occluded = true});
      };

      const auto line = [&](const vec3f& start, const vec3f& end, color_t color)
      { scene.debug.line(start, end, color, 0.f, 0.f, /*draw_when_occluded*/ true); };

      for (const assets::posed_hitbox_t &hitbox : volumes)
      {
        const color_t color = client::hit_region_color(hitbox.region);
        client::draw_posed_hitbox_faces(face, hitbox, with_alpha(color, HITBOX_FACE_ALPHA));

        // Fully faded is SKIPPED, not appended at zero alpha: it is ~200 line
        // segments per player, and they would still be built, stored and
        // recorded to draw nothing.
        if (edge_fade > 0.0f)
          client::draw_posed_hitbox(line, hitbox,
                                    with_alpha(color, color_channel_from_float(edge_fade)));
      }
    }
  }
}

void Play_State::draw_debug_overlays(client_context_t &ctx, const draw_inputs_t &frame)
{
  shared::Entity_System &entity_system = ctx.world.session.entity_system;


  // Debug: navmesh as triangle wireframes, colored by island ID
  if (ctx.cvars->debug_show_navmesh)
  {
    const shared::navmesh_t &nav = ctx.world.session.navmesh;
    constexpr float y_lift = 2.f;

    static constexpr color_t island_colors[] = {
      colors::cyan,
      colors::yellow,
      colors::green,
      colors::magenta,
    };

    for (const auto &poly : nav.polygons)
    {
      color_t color = island_colors[poly.island % 4];
      const int N = (int)poly.vertices.size();
      for (int e = 0; e < N; ++e)
      {
        vec3f a = nav.vertices[poly.vertices[e          ]].position;
        vec3f b = nav.vertices[poly.vertices[(e + 1) % N]].position;
        a.y += y_lift;
        b.y += y_lift;
        scene.debug.line(a, b, color);
      }
    }

    constexpr float r = 2.f;
    constexpr color_t vert_color = colors::white;
    for (const auto &v : nav.vertices)
    {
      vec3f p = v.position; p.y += y_lift;
      scene.debug.line({p.x - r, p.y, p.z}, {p.x + r, p.y, p.z}, vert_color);
      scene.debug.line({p.x, p.y, p.z - r}, {p.x, p.y, p.z + r}, vert_color);
    }
  }

  // Debug: collision faces in green. Translucent, so they do not write depth and
  // the geometry behind them stays readable -- which is what the alpha in the
  // colour now actually buys.
  if (ctx.cvars->debug_show_collisions)
  {
    const color_t face_color = with_alpha(colors::green, 128);

    for (const Debug_Collision_Face &face : ctx.visuals.debug_collision_faces)
    {
      if (!face.polygon.empty())
        scene.debug.filled_polygon(face.polygon, face_color);

      vec3f arrow_start = face.plane.point + face.plane.normal * 0.5f;
      vec3f arrow_end = arrow_start + face.plane.normal * 5.0f;
      scene.debug.line(arrow_start, arrow_end, colors::red);
    }

    ctx.visuals.debug_collision_faces.clear();
  }

  // Debug: collision volumes as wireframe AABBs. Magenta for every Box_Volume
  // (triggers, pads, reflection volumes, a damageable's hitbox), white for the
  // map's geometry.
  if (ctx.cvars->debug_show_box_volumes)
  {
    for (auto [entity, volume] : entity_system.entities_with<entities::Box_Volume>())
    {
      if (entity.type == entities::entity_type::Weapon_Entity &&
          static_cast<const entities::Weapon_Entity&>(entity).owner_uid != shared::null_entity_uid)
        continue;

      const shared::aabb_bounds_t bounds = shared::get_bounds(volume, entity.position);
      scene.debug.aabb(bounds.min, bounds.max, colors::magenta);
    }

    for (const shared::map_geometry_t &entry : ctx.world.session.geometry)
    {
      const shared::aabb_bounds_t bounds = shared::get_bounds(entry.value);
      scene.debug.aabb(bounds.min, bounds.max, colors::white);
    }
  }

  // --- Bot path / goal debug draw ---
  {
    static constexpr color_t goal_color[] = {
      color_t{136, 136, 136},  // Idle
      color_t{255, 0, 255},    // Chase
      colors::red,             // Attack
      color_t{0, 68, 255},     // Retreat
    };

    for (const auto &bot : ctx.replication.bot_debug_entries)
    {
      int gi = static_cast<int>(bot.goal);
      color_t color = goal_color[gi < 4 ? gi : 0];

      const auto &path = bot.path;
      for (int i = bot.path_index; i + 1 < (int)path.size(); ++i)
      {
        vec3f a = path[i];     a.y += 4.f;
        vec3f b = path[i + 1]; b.y += 4.f;
        scene.debug.line(a, b, color);
      }

      if (bot.path_index < (int)path.size())
      {
        vec3f wp = path[bot.path_index]; wp.y += 4.f;
        constexpr float r = 8.f;
        scene.debug.line({wp.x - r, wp.y, wp.z}, {wp.x + r, wp.y, wp.z}, color);
        scene.debug.line({wp.x, wp.y, wp.z - r}, {wp.x, wp.y, wp.z + r}, color);
      }

      const entities::Player_Entity* bot_player = try_find_player_in_slot(ctx, bot.slot);
      if (bot_player != nullptr)
      {
        const entities::Player_Entity &ent = *bot_player;
        vec3f facing = linalg::direction_from_angles(ent.view_angle_yaw, 0.f);
        vec3f origin = ent.position;
        origin.y += 40.f;
        constexpr float arrow_len = 30.f;
        vec3f tip = origin + facing * arrow_len;
        scene.debug.line(origin, tip, colors::white);
      }
    }
  }
}

void Play_State::draw_particles(client_context_t &ctx, const draw_inputs_t &frame)
{
  shared::Entity_System &entity_system = ctx.world.session.entity_system;
  for (const entities::Particle_Emitter_Entity &emitter :
       entity_system.entities_of_type<entities::Particle_Emitter_Entity>())
    scene.particles.push_back(emitter_parameters(emitter, frame.world_delta_seconds));

  for (const auto &fx : ctx.visuals.explosion_effects)
    scene.particles.push_back(
        explosion_parameters(fx.explosion_index, fx.position, fx.time_remaining, frame.world_delta_seconds));

  for (const afterimage_t &afterimage : ctx.visuals.afterimages)
    scene.particles.push_back(afterimage_dust_parameters(afterimage, ctx.cvars->cl_afterimage_seconds,
                                                         frame.world_delta_seconds));
}

void Play_State::draw_hud(client_context_t &ctx, renderer::ui_draw_list_t &ui)
{
  // --- Screen-space UI ---
  // Only while actually looking around: an uncaptured cursor is the player's
  // aiming device at that point, and a second one in the middle reads as a bug.
  if (connection_ui.mouse_captured && !connection_ui.show_pause_menu &&
      ctx.cvars->cl_crosshair)
  {
    hud::crosshair_settings_t crosshair;
    crosshair.arm_length = ctx.cvars->cl_crosshair_size;
    crosshair.gap        = ctx.cvars->cl_crosshair_gap;
    crosshair.thickness  = ctx.cvars->cl_crosshair_thickness;
    crosshair.draw_dot   = ctx.cvars->cl_crosshair_dot;
    crosshair.color      = {clamp_crosshair_color_channel(ctx.cvars->cl_crosshair_r),
                            clamp_crosshair_color_channel(ctx.cvars->cl_crosshair_g),
                            clamp_crosshair_color_channel(ctx.cvars->cl_crosshair_b),
                            clamp_crosshair_color_channel(ctx.cvars->cl_crosshair_a)};
    hud::draw_crosshair(ui, renderer::screen_size(), renderer::display_scale(), crosshair);
  }

  
  if (ctx.cvars->cl_show_deploy_timer && !connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t *font = ctx.font)
    {
      // Now: the tick the next input is predicted to run in, plus the part of it drawn so far.
      const float tick_dt = shared::tick_interval_seconds(ctx.connection.server_tickrate);
      const shared::subtick_time_t now =
          shared::subtick_time(get_predicted_server_tick_which_this_input_will_be_simulated_on(
                                   ctx, ctx.prediction.input_number),
                               0) +
          static_cast<shared::subtick_time_t>(ctx.prediction.physics_accumulator / tick_dt *
                                              static_cast<float>(shared::SUBTICK_SLOT_COUNT));
      hud::draw_deploy_timer(ui, *font, renderer::screen_size(), renderer::display_scale(),
                             shared::subtick_seconds_between(now, ctx.prediction.state.inventory.deploy_complete_time,
                                                             tick_dt));
    }
    else
      log_error("[hud] no UI font registered; cl_show_deploy_timer cannot draw");
  }

  const entities::Player_Entity* viewed_player = try_find_viewed_player(ctx);
  if (!connection_ui.show_pause_menu && viewed_player != nullptr)
  {
    if (const ui::ui_font_t* font = ctx.font)
    {
      const shared::weapon_definition_t* held_weapon = try_find_weapon_definition_for_active_weapon_held_by_player(ctx, viewed_player);
      const entities::Weapon_Entity* held_entity =
          held_weapon != nullptr ? try_find_active_weapon(ctx, *viewed_player) : nullptr;
      hud::draw_weapon_name(ui, *font, renderer::screen_size(), renderer::display_scale(),
                            held_weapon != nullptr ? held_weapon->display_name : "Empty",
                            held_entity != nullptr ? held_entity->ammo : -1,
                            held_entity != nullptr ? held_entity->reserve_ammo : -1);
    }
    else
    {
      log_error("[hud] no UI font registered; the weapon name cannot draw");
    }
  }

  const entities::Match *match = client::try_find_match(ctx);
  if (match != nullptr && match->phase == entities::Round_Phase::Live && !connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t* font = ctx.font)
    {
      const float tick_dt = shared::tick_interval_seconds(ctx.connection.server_tickrate);
      const int64_t ticks_elapsed = static_cast<int64_t>(ctx.replication.latest_processed_tick) -
                                    static_cast<int64_t>(match->phase_start_tick);
      hud::draw_run_timer(ui, *font, renderer::screen_size(), renderer::display_scale(),
                          static_cast<float>(ticks_elapsed) * tick_dt);
    }
    else
    {
      log_error("[hud] no UI font registered; the run timer cannot draw");
    }
  }

  if (match != nullptr && match->phase == entities::Round_Phase::Freeze && !connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t* font = ctx.font)
    {
      const int64_t ticks_left = static_cast<int64_t>(match->phase_end_tick) -
                                 static_cast<int64_t>(ctx.replication.latest_processed_tick);

      int32_t joined = 0;
      int32_t voted  = 0;
      for (const entities::Player_Entity& player :
           ctx.world.session.entity_system.entities_of_type<entities::Player_Entity>())
      {
        if (player.client_slot_index < 0 || player.client_slot_index >= network::sv_max_client_count)
          continue;
        ++joined;
        if (player.wants_to_skip_freeze)
          ++voted;
      }
      const std::string caption = joined > 1 ? std::format("SPACE to skip  {}/{}", voted, joined)
                                             : std::string{"SPACE to skip"};

      hud::draw_freeze_countdown(ui, *font, renderer::screen_size(), renderer::display_scale(),
                                 static_cast<float>(ticks_left) * shared::tick_interval_seconds(ctx.connection.server_tickrate),
                                 caption);
    }
    else
    {
      log_error("[hud] no UI font registered; the freeze countdown cannot draw");
    }
  }

  // A Round_End with a deadline that the objective caused is the hold before next_map loads.
  if (match != nullptr && match->phase == entities::Round_Phase::Round_End &&
      match->end_reason == entities::Round_End_Reason::Objective && match->phase_end_tick != 0 &&
      !connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t* font = ctx.font)
    {
      const int64_t ticks_left = static_cast<int64_t>(match->phase_end_tick) -
                                 static_cast<int64_t>(ctx.replication.latest_processed_tick);
      hud::draw_freeze_countdown(ui, *font, renderer::screen_size(), renderer::display_scale(),
                                 static_cast<float>(ticks_left) * shared::tick_interval_seconds(ctx.connection.server_tickrate),
                                 "NEXT LEVEL  (R to restart)");
    }
    else
    {
      log_error("[hud] no UI font registered; the next-map countdown cannot draw");
    }
  }

  if (match != nullptr && shared::is_before_match(match->phase) && !connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t* font = ctx.font)
    {
      hud::warmup_vote_view_t vote;
      vote.starts_when_loaded = match->starts_when_loaded;
      if (match->phase == entities::Round_Phase::Countdown)
      {
        const int64_t ticks_left = static_cast<int64_t>(match->phase_end_tick) -
                                   static_cast<int64_t>(ctx.replication.latest_processed_tick);
        vote.seconds_until_start =
            static_cast<float>(ticks_left) * shared::tick_interval_seconds(ctx.connection.server_tickrate);
      }
      for (const entities::Player_Entity& player :
           ctx.world.session.entity_system.entities_of_type<entities::Player_Entity>())
      {
        if (player.client_slot_index < 0 || player.client_slot_index >= network::sv_max_client_count)
          continue;
        ++vote.joined;
        if (player.ready)
          ++vote.ready;
      }
      if (const entities::Player_Entity* my_player = try_find_my_player(ctx))
      {
        vote.i_have_a_body = true;
        vote.i_am_ready    = my_player->ready;
      }
      hud::draw_ready_status(ui, *font, renderer::screen_size(), renderer::display_scale(), vote);
    }
    else
    {
      log_error("[hud] no UI font registered; the warmup vote cannot draw");
    }
  }

  if (input::is_key_down(input::key_t::Tab) && !connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t* font = ctx.font)
    {
      const Span<hud::scoreboard_row_t> rows = hud::collect_scoreboard_rows(
          ctx.world.session.entity_system.entities_of_type<entities::Player_Entity>(),
          ctx.connection.my_slot, scoreboard_rows);
      hud::draw_scoreboard(ui, *font, renderer::screen_size(), renderer::display_scale(), rows);
    }
    else
    {
      log_error("[hud] no UI font registered; the scoreboard cannot draw");
    }
  }
}

void Play_State::draw(float delta_seconds, std::vector<renderer::view_pass_t> &passes,
                             renderer::ui_draw_list_t &ui)
{
  auto &ctx = state_manager::get_client_context();
  draw_inputs_t frame;
  frame.world_delta_seconds = ctx.connection.phase == Connection_Phase::Replaying
                                  ? replay_world_dt(ctx.replay, delta_seconds)
                                  : delta_seconds;

  if (connection_ui.show_pause_menu)
  {
    if (const ui::ui_font_t* font = ctx.font)
      ui::draw_screen(ui, pause_menu.screen, *font);
    else
      log_error("[menu] no UI font registered; the pause menu cannot draw");
  }

  if (!ctx.world.ready)
    return;

  // Clears the mesh and emitter lists and RETIRES the debug list -- so a trace
  // appended with a lifetime from a fixed tick survives, and one appended with
  // none dies here, exactly one frame after it was made.
  scene.begin_frame(delta_seconds);
  scene.view.viewport = {{0, 0}, {1, 1}};
  scene.view.camera   = camera;
  scene.debug_channel = ctx.cvars->r_debug_channel;
  if (ctx.cvars->r_shadow_freeze)
  {
    draw_shadow_cascades(scene.debug, renderer::sun_shadow_cascades());
    draw_point_shadow_faces(scene.debug, renderer::point_shadow_faces());
  }
  pose_count = 0;

  frame_timing::lap_zone_t lap("draw: hidden set, blob shadow, geometry");

  shared::compute_hidden_geometry(ctx.world.session.entity_system, ctx.world.session.owner_of, frame.hidden);
  frame.blob_shadow      = {.radius       = ctx.cvars->cl_blob_shadow_radius,
                            .opacity      = ctx.cvars->cl_blob_shadow_opacity,
                            .max_distance = ctx.cvars->cl_blob_shadow_max_distance};
  frame.camera_is_my_eye = ctx.connection.phase == Connection_Phase::Connected && !ctx.connection.spectating &&
                           !ctx.cvars->cl_free_camera && ctx.cvars->cl_spectate_slot < 0;
  frame.drawn            = get_tick_and_fraction_used_for_drawing_this_frame(ctx);
  frame.local_feet       = drawn_local_feet(ctx);
  frame.held_flashlight  = {.color          = {ctx.cvars->r_flashlight_red, ctx.cvars->r_flashlight_green,
                                               ctx.cvars->r_flashlight_blue},
                            .intensity      = ctx.cvars->r_flashlight_intensity,
                            .inner_fraction = ctx.cvars->r_flashlight_inner};

  if (frame.camera_is_my_eye)
    draw_player_blob_shadow(ctx, frame, frame.local_feet);

  lap.next("draw: geometry loop");
  draw_map_geometry(ctx, frame);

  lap.next("draw: debug readouts, reveal cones, fog, lenses");
  draw_shadow_debug_and_volumes(ctx, frame);

  lap.next("draw: lights + beams");
  draw_lights_and_beams(ctx, frame);

  lap.next("draw: rendered entities");
  draw_rendered_entities(ctx, frame);

  lap.next("draw: afterimages + ghosts");
  draw_afterimages_and_ghosts(ctx, frame);

  lap.next("draw: remote players");
  draw_remote_players(ctx, frame);

  lap.next("draw: debug overlays + particles");
  draw_debug_overlays(ctx, frame);
  draw_particles(ctx, frame);

  lap.next("draw: sky, grabbed frames, to_pass");
  scene.sky = skybox.resolve(ctx.cvars->sv_skybox.c_str());

  place_grabbed_frames();

  passes.push_back(scene.to_pass());

  lap.next("draw: hud");
  draw_hud(ctx, ui);
}

} // namespace client
