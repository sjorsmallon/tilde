#include "geometry_renderer.hpp"
#include "../shared/aabb.hpp"
#include "../shared/assets/asset.hpp"
#include "../shared/color.hpp"
#include "../shared/frame_timing.hpp"
#include "../shared/log.hpp"
#include "../shared/map/map_geometry_asset.hpp"
#include "../shared/map/map_geometry_mesh.hpp"
#include "render_assets.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace client
{

namespace
{

// A GENERATED mesh (an inline mesh's drawn form, a lightmapped asset copy)
// lives in the asset cache under a synthetic path keyed by uid, so one only
// gets rebuilt when it actually changes. The key is per OBJECT, not per kind: a
// uid names one object and one object has one generated mesh.
// Formats into CALLER-OWNED storage and returns a view of it -- no allocation.
//
// It used to return a std::string built as `"__geometry_" + std::to_string(uid)`,
// which is ~21 characters and therefore past the small-string buffer: one heap
// allocation per surface PER FRAME, immediately discarded. Paired with the
// temporary string the pool lookup used to build (see transparent_string_hash_t
// in asset_types.hpp), the per-frame geometry draw path allocated twice per
// surface to ask a question whose answer it threw away.
//
// The out-param is about STORAGE, not about the return value, which is exactly
// the case CLAUDE.md's failure convention keeps a Span for.
std::string_view generated_mesh_cache_key(shared::entity_uid_t uid, Span<char> buffer)
{
  const int written =
      std::snprintf(buffer.data, buffer.count, "__geometry_%llu",
                    static_cast<unsigned long long>(uid));
  if (written <= 0)
    fatal_error("generated_mesh_cache_key: could not format uid {}", uid);
  if (static_cast<uint32_t>(written) >= buffer.count)
    fatal_error("generated_mesh_cache_key: buffer of {} bytes is too small for uid {}",
                buffer.count, uid);
  return std::string_view(buffer.data, static_cast<size_t>(written));
}

// What each cached generated mesh was built from.
//
// A generated mesh is WORLD-space with an identity transform, so the cache goes
// stale when an object MOVES, not only when it changes shape. And an object
// moves from the gizmo, from undo, from the inspector, from the map file --
// asking every one of those to remember a refresh call is how the grey solid
// ends up sitting still while the contour lines walk away from it, which is
// exactly what happened.
//
// So the draw path checks instead of trusting: a cache nothing can forget to
// invalidate. A session's list has ONE writer, build_session, so it is checked
// once there instead.
//
// It holds the whole geometry VALUE rather than just the vertices, because a
// face's material is part of what the mesh was built from and changing one
// moves no vertex. And the material TABLE beside it, because an index resolves
// against that table: retexturing entry 3 changes the mesh of every object
// whose faces name it, with nothing about the objects themselves different.
struct generated_mesh_source_t
{
  shared::mesh_geometry_t value;
  std::vector<std::string> materials;
  // The bake the UVs came from. A rebake moves charts in the atlas without
  // moving a vertex or changing a material, so neither of the two above can
  // notice one.
  uint32_t lightmap_geometry_id = 0;
  // Per submesh slot. Rebuilt with the mesh, so the two cannot describe
  // different objects.
  std::vector<renderer::material_handle_t> overrides;
};

std::unordered_map<shared::entity_uid_t, generated_mesh_source_t> g_generated_mesh_sources;

bool generated_mesh_is_current(shared::entity_uid_t uid,
                           const shared::mesh_geometry_t &geometry,
                           Span<const std::string> materials,
                           const shared::lightmap_t &lightmap)
{
  const auto it = g_generated_mesh_sources.find(uid);
  if (it == g_generated_mesh_sources.end())
    return false;

  if (it->second.lightmap_geometry_id != lightmap.geometry_id)
    return false;

  if (it->second.materials.size() != materials.count)
    return false;
  for (size_t i = 0; i < materials.count; ++i)
    if (it->second.materials[i] != materials[i])
      return false;

  // Bit-exact, which is what geometry_values_equal already promises -- anything
  // looser would let a sub-tolerance move leave the solid behind its contours.
  return shared::geometry_values_equal(it->second.value, geometry);
}

// The generated form of a mesh: an inline mesh baked through its pose, or the
// lightmapped world-space copy of an asset-sourced one.
assets::mesh_asset_t generate_geometry_mesh(const shared::mesh_geometry_t &mesh,
                                            shared::entity_uid_t uid,
                                            Span<const std::string> materials,
                                            const shared::lightmap_t &lightmap)
{
  if (std::holds_alternative<shared::asset_mesh_source_t>(mesh.source))
    return shared::generate_lightmapped_asset_mesh(mesh, {&lightmap, uid});
  return shared::generate_mesh_geometry_mesh(mesh, materials, {&lightmap, uid});
}

// An UNTEXTURED face is BLOCKOUT geometry, and a tiling dev texture is the
// wrong thing to read it by: it describes surface where what matters is SHAPE
// and SIZE. Flat light grey under shader_t::grid, which rules the world grid
// onto the face -- so it reports its own dimensions, and the lines run on across
// a corner and on into the editor's floor grid.
//
// The grid costs no material parameter and no push-constant byte (the mesh block
// is at the 128-byte minimum with none left): a default face UV is the world
// position projected on the face's dominant axis over the 128-unit cell, so the
// shader reads the grid straight out of fragUV.
//
// No texture handle at all -- an invalid one resolves to the internal 1x1 white
// inside the renderer's material set, so this is base_color and nothing else.
renderer::material_handle_t blockout_material()
{
  static const renderer::material_handle_t handle = [] {
    renderer::material_t built{};
    built.pipeline_state.shader = renderer::shader_t::grid;
    built.parameters.base_color = {0.72f, 0.72f, 0.75f, 1.0f};
    return renderer::register_material(built);
  }();
  return handle;
}

namespace
{
struct face_material_key_t
{
  uint32_t handles[renderer::MATERIAL_MAP_COUNT * BLEND_LAYER_COUNT] = {};
  uint32_t state = 0;

  bool operator==(const face_material_key_t &) const = default;
};

struct face_material_key_hash_t
{
  size_t operator()(const face_material_key_t &key) const
  {
    size_t hash = 1469598103934665603ull;
    for (uint32_t handle : key.handles)
      hash = (hash ^ handle) * 1099511628211ull;
    return (hash ^ key.state) * 1099511628211ull;
  }
};

void write_layer_key(face_material_key_t &key, int layer, const renderer::material_maps_t &maps)
{
  const uint32_t handles[renderer::MATERIAL_MAP_COUNT] = {maps.albedo.index, maps.normal.index, maps.orm.index,
                                                           maps.height.index, maps.emissive.index};
  memcpy(&key.handles[layer * renderer::MATERIAL_MAP_COUNT], handles, sizeof(handles));
}

uint32_t pipeline_state_key(const renderer::pipeline_state_t &state)
{
  return (uint32_t)state.shader | ((uint32_t)state.blend_mode << 4) | ((uint32_t)state.cull_mode << 6) |
         ((uint32_t)state.depth_test << 7) | ((uint32_t)state.depth_write << 8) |
         ((uint32_t)state.alpha_cutoff << 9);
}
} // namespace

// One material per distinct (maps, state), not per submesh and not per rebuild:
// a brush mesh is re-registered on every edit, and registering a fresh material
// each time would grow the renderer's table for the length of an editing
// session. The state is the registered slot's own, so the shader the face's
// material named is the shader the face is drawn by.
renderer::material_handle_t face_material(const renderer::material_parameters_t &parameters,
                                          const renderer::pipeline_state_t      &state)
{
  static std::unordered_map<face_material_key_t, renderer::material_handle_t, face_material_key_hash_t> by_key;

  face_material_key_t key{};
  write_layer_key(key, 0, parameters.maps);
  for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
    write_layer_key(key, layer, parameters.blend_maps.data[layer - 1]);
  key.state = pipeline_state_key(state);

  const auto cached = by_key.find(key);
  if (cached != by_key.end())
    return cached->second;

  renderer::material_t built{};
  built.pipeline_state = state;
  built.parameters     = parameters;

  const renderer::material_handle_t handle = renderer::register_material(built);
  by_key.emplace(key, handle);
  return handle;
}

// The override table for a generated brush or mesh: one entry per submesh slot,
// grid-shaded where a Lit face's material resolved to no texture and the slot's
// own state everywhere else. Built from the mesh's OWN materials, which the
// generator already filled with the resolved texture for each slot -- so a
// slot naming a texture that failed to load carries the renderer's magenta
// checkerboard and is visible rather than silently flat.
//
// Stored beside the source record so it is rebuilt exactly when the mesh is, and
// the two cannot describe different objects.
std::vector<renderer::material_handle_t>
build_generated_material_overrides(renderer::mesh_handle_t mesh)
{
  std::vector<renderer::material_handle_t> table;
  for (renderer::material_handle_t slot : renderer::mesh_default_materials(mesh))
  {
    const renderer::material_parameters_t parameters = renderer::material_parameters(slot);
    const renderer::pipeline_state_t      state      = renderer::material_pipeline_state(slot);

    const bool blockout = state.shader == renderer::shader_t::lit && !parameters.maps.albedo.valid();
    table.push_back(blockout ? blockout_material() : face_material(parameters, state));
  }
  return table;
}

// Draw a surface's mesh if it resolves. False means "no mesh — use the kind's
// own primitive".
renderer::clock_wipe_t moved_clock_wipe(const renderer::clock_wipe_t& clock_wipe,
                                        const linalg::mat4f* moved_by)
{
  if (moved_by == nullptr || !clock_wipe.armed)
    return clock_wipe;

  const auto moved = [&](const linalg::vec3f& value, float w)
  {
    const linalg::vec4 result = *moved_by * linalg::vec4{value.x, value.y, value.z, w};
    return linalg::vec3f{result.x, result.y, result.z};
  };
  return {.center = moved(clock_wipe.center, 1.0f),
          .axis_x = moved(clock_wipe.axis_x, 0.0f),
          .axis_y = moved(clock_wipe.axis_y, 0.0f),
          .wiped  = clock_wipe.wiped,
          .armed  = true};
}

// A team wall over whatever the draw would have worn. Passable: the ghost shader,
// the tint the colour, the rim the shape, and a translucent thing casts no
// shadow. Not passable: the same materials with the team's colour multiplied in.
void team_wall_draw(renderer::mesh_draw_t& draw, const team_wall_tint_t& team_wall)
{
  draw.tint = team_wall.color;
  if (!team_wall.passable)
    return;

  draw.material_overrides = ghost_variant(draw.mesh);
  draw.shadow_caster      = renderer::shadow_caster_t::none;
}

void light_cut_draw(renderer::mesh_draw_t& draw, renderer::light_cut_t light_cut)
{
  draw.light_cut = light_cut;
  if (light_cut == renderer::light_cut_t::revealed || light_cut == renderer::light_cut_t::shadow_solid ||
      renderer::light_cut_is_a_lens(light_cut))
    draw.shadow_caster = renderer::shadow_caster_t::none;
}

bool draw_surface_mesh(draw_lists_t &draws, const shared::geometry_surface_t &surface,
                       renderer::mesh_handle_t mesh, const linalg::mat4f &transform,
                       const linalg::mat4f* moved_by, const renderer::clock_wipe_t& clock_wipe,
                       std::optional<team_wall_tint_t> team_wall, renderer::light_cut_t light_cut,
                       bool casts_shadows, bool casts_dynamic_shadow)
{
  if (!surface.visible)
    return true; // resolved to "draw nothing", which is not a fallback case

  if (!mesh.valid())
    return false;

  renderer::mesh_draw_t draw{};
  draw.mesh          = mesh;
  draw.transform     = moved_by != nullptr ? *moved_by * transform : transform;
  draw.shadow_caster = !casts_shadows ? renderer::shadow_caster_t::none
                       : (moved_by != nullptr || casts_dynamic_shadow)
                           ? renderer::shadow_caster_t::dynamic_object
                           : renderer::shadow_caster_t::static_geometry;
  draw.clock_wipe    = moved_clock_wipe(clock_wipe, moved_by);

  if (surface.is_wireframe)
  {
    draw.fill = renderer::fill_mode_t::wireframe;
  }
  else
  {
    draw.tint               = color_from_vec3(surface.color);
    if (team_wall)
      team_wall_draw(draw, *team_wall);
  }
  light_cut_draw(draw, light_cut);

  draws.meshes.push_back(draw);
  return true;
}

// A mesh through the generated cache. The generated mesh is baked through its
// pose into WORLD space, so the transform is identity.
void draw_generated_geometry(draw_lists_t &draws, const shared::mesh_geometry_t &geometry,
                             shared::entity_uid_t uid, Span<const std::string> materials,
                             const shared::lightmap_t &lightmap,
                             const linalg::mat4f* moved_by, const renderer::clock_wipe_t& clock_wipe,
                             std::optional<team_wall_tint_t> team_wall,
                             renderer::light_cut_t light_cut, bool casts_shadows,
                             bool casts_dynamic_shadow, bool compare)
{
  frame_timing::lap_zone_t lap("draw_geometry: generated is_current");
  if (compare && !generated_mesh_is_current(uid, geometry, materials, lightmap))
  {
    lap.next("draw_geometry: generated refresh (rebuild)");
    refresh_generated_geometry_mesh(geometry, uid, materials, lightmap);
  }

  lap.next("draw_geometry: generated lookups + draw record");
  char                   cache_key_buffer[48];
  const std::string_view cache_key = generated_mesh_cache_key(uid, cache_key_buffer);
  const renderer::mesh_handle_t mesh = get_render_mesh(assets::find_mesh_in_cache(cache_key));
  if (!mesh.valid())
  {
    log_error("draw_geometry: could not build a mesh for uid {}", uid);
    return;
  }

  renderer::mesh_draw_t draw{};
  draw.mesh          = mesh;
  draw.transform     = moved_by != nullptr ? *moved_by : linalg::mat4f::identity();
  draw.shadow_caster = !casts_shadows ? renderer::shadow_caster_t::none
                       : (moved_by != nullptr || casts_dynamic_shadow)
                           ? renderer::shadow_caster_t::dynamic_object
                           : renderer::shadow_caster_t::static_geometry;
  draw.clock_wipe    = moved_clock_wipe(clock_wipe, moved_by);

  const auto source = g_generated_mesh_sources.find(uid);
  if (source != g_generated_mesh_sources.end())
    draw.material_overrides = source->second.overrides;

  if (team_wall)
    team_wall_draw(draw, *team_wall);
  light_cut_draw(draw, light_cut);

  draws.meshes.push_back(draw);
}

} // namespace

renderer::clock_wipe_t geometry_wipe_frame(const shared::mesh_geometry_t& geometry,
                                           const linalg::quatf& orientation)
{
  const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
  return renderer::clock_wipe_t{.center = (bounds.min + bounds.max) * 0.5f,
                                .axis_x = linalg::rotate(orientation, linalg::vec3f{1.0f, 0.0f, 0.0f}),
                                .axis_y = linalg::rotate(orientation, linalg::vec3f{0.0f, 0.0f, 1.0f}),
                                .wiped  = 0.0f,
                                .armed  = false};
}

void draw_geometry(draw_lists_t &draws, const shared::mesh_geometry_t &mesh,
                   shared::entity_uid_t uid, Span<const std::string> materials,
                   const shared::lightmap_t &lightmap, const linalg::mat4f* moved_by,
                   const renderer::clock_wipe_t& clock_wipe,
                   std::optional<team_wall_tint_t> team_wall, renderer::light_cut_t light_cut,
                   bool casts_dynamic_shadow, generated_mesh_check_t check)
{
  const shared::geometry_surface_t &surface = mesh.surface;
  // A trusted object with no record yet is built, never drawn from nothing.
  const bool compare = check == generated_mesh_check_t::Compare_Every_Draw ||
                       !g_generated_mesh_sources.contains(uid);

  if (!std::holds_alternative<shared::asset_mesh_source_t>(mesh.source))
  {
    if (!surface.visible)
      return;
    draw_generated_geometry(draws, mesh, uid, materials, lightmap, moved_by, clock_wipe,
                            team_wall, light_cut, mesh.casts_shadows, casts_dynamic_shadow,
                            compare);
    return;
  }

  // With a bake, an asset-sourced mesh draws as its own WORLD-space copy
  // through the generated cache -- the pooled asset is welded and shared
  // between maps, so it cannot carry this instance's atlas positions. Without
  // one it is the pooled asset under its transform.
  if (!lightmap.empty())
  {
    if (compare && !generated_mesh_is_current(uid, mesh, materials, lightmap))
      refresh_generated_geometry_mesh(mesh, uid, materials, lightmap);

    char                   cache_key_buffer[48];
    const std::string_view cache_key = generated_mesh_cache_key(uid, cache_key_buffer);
    if (draw_surface_mesh(draws, surface, get_render_mesh(assets::find_mesh_in_cache(cache_key)),
                          linalg::mat4f::identity(), moved_by, clock_wipe, team_wall,
                          light_cut, mesh.casts_shadows, casts_dynamic_shadow))
      return;
  }
  else if (draw_surface_mesh(draws, surface,
                             get_render_mesh(
                                 shared::resolve_mesh_asset(shared::get_mesh_asset_source(mesh))),
                             shared::compute_asset_mesh_transform(mesh), moved_by, clock_wipe,
                             team_wall, light_cut, mesh.casts_shadows, casts_dynamic_shadow))
    return;

  // An asset that does not resolve has nothing to draw but its bound — shown
  // in yellow so a broken or missing asset is visible rather than absent.
  const shared::aabb_bounds_t bounds = shared::get_bounds(mesh);
  draws.debug.aabb(bounds.min, bounds.max, colors::yellow);
}

void draw_mesh_edges(draw_lists_t &draws, const shared::mesh_geometry_t &mesh, color_t color,
                     float depth_bias, const linalg::mat4f &world)
{
  const std::vector<shared::mesh_directed_edge_t> edges = shared::collect_mesh_directed_edges(mesh);
  const auto placed = [&](uint32_t vertex) -> linalg::vec3
  {
    const linalg::vec3 posed = mesh.position + linalg::rotate(mesh.orientation, mesh.vertices[vertex]);
    const linalg::vec4 moved = world * linalg::vec4{posed.x, posed.y, posed.z, 1.0f};
    return {moved.x, moved.y, moved.z};
  };
  for (const shared::mesh_directed_edge_t &edge : edges)
  {
    if (edge.a > edge.b && shared::try_find_mesh_face_walking_edge(edges, edge.b, edge.a))
      continue;
    draws.debug.line(placed(edge.a), placed(edge.b), color, depth_bias);
  }
}

void draw_geometry_wireframe(draw_lists_t &draws, const shared::mesh_geometry_t &mesh,
                             color_t color, float depth_bias, const linalg::mat4f* moved_by)
{
  const linalg::mat4f world = moved_by != nullptr ? *moved_by : linalg::mat4f::identity();
  if (!std::holds_alternative<shared::asset_mesh_source_t>(mesh.source))
  {
    draw_mesh_edges(draws, mesh, color, depth_bias, world);
    return;
  }

  const renderer::mesh_handle_t render_mesh =
      get_render_mesh(shared::resolve_mesh_asset(shared::get_mesh_asset_source(mesh)));
  if (!render_mesh.valid() || !renderer::wireframe_supported())
    return;
  renderer::mesh_draw_t draw{};
  draw.mesh      = render_mesh;
  draw.transform = world * shared::compute_asset_mesh_transform(mesh);
  draw.tint      = color;
  draw.fill      = renderer::fill_mode_t::wireframe;
  draws.meshes.push_back(draw);
}

namespace
{

void rebuild_generated_geometry_mesh(const shared::mesh_geometry_t &geometry,
                                     shared::entity_uid_t uid,
                                     Span<const std::string> materials,
                                     const shared::lightmap_t &lightmap)
{
  const bool generates_materials = shared::find_asset_sourced_mesh(geometry) == nullptr;

  // Record what the mesh is about to be built from, so the draw path can tell
  // whether it still matches. A lightmapped asset-sourced mesh is baked into
  // world space like the rest, so a move goes stale the same way.
  {
    generated_mesh_source_t &source = g_generated_mesh_sources[uid];
    source.value                     = geometry;
    source.materials.assign(materials.begin(), materials.end());
    source.lightmap_geometry_id = lightmap.geometry_id;
  }

  char                   cache_key_buffer[48];
  const std::string_view cache_key = generated_mesh_cache_key(uid, cache_key_buffer);
  assets::asset_handle_t<assets::mesh_asset_t> mesh_asset =
      assets::find_mesh_in_cache(cache_key);

  if (!mesh_asset.valid())
  {
    mesh_asset = assets::register_dynamic_mesh(
        cache_key, generate_geometry_mesh(geometry, uid, materials, lightmap));
  }
  else
  {
    assets::mesh_asset_t *mesh = assets::get_mutable(mesh_asset);
    if (!mesh)
    {
      log_error("refresh_generated_geometry_mesh: uid {} is cached but not mutable", uid);
      return;
    }

    *mesh = generate_geometry_mesh(geometry, uid, materials, lightmap);

    // Eager re-upload, right here where the edit happened, rather than a flag
    // the next draw would have to notice.
    renderer::update_mesh(get_render_mesh(mesh_asset), *mesh);
    forget_material_variants(get_render_mesh(mesh_asset));
  }

  // The slot table follows the mesh it describes -- a rebuild can change how
  // many submeshes there are, so a stale table would name the wrong material for
  // every slot after the one that moved.
  if (generates_materials)
    g_generated_mesh_sources[uid].overrides =
        build_generated_material_overrides(get_render_mesh(mesh_asset));
}

} // namespace

void refresh_generated_geometry_mesh(const shared::mesh_geometry_t &geometry,
                                     shared::entity_uid_t uid,
                                     Span<const std::string> materials,
                                     const shared::lightmap_t &lightmap)
{
  rebuild_generated_geometry_mesh(geometry, uid, materials, lightmap);
}

void refresh_stale_generated_geometry_meshes(Span<const shared::map_geometry_t> geometry,
                                             Span<const std::string> materials,
                                             const shared::lightmap_t &lightmap)
{
  for (const shared::map_geometry_t &entry : geometry)
  {
    const bool generated = shared::find_asset_sourced_mesh(entry.value) != nullptr
                               ? !lightmap.empty()
                               : shared::get_surface(entry.value).visible;
    if (generated && !generated_mesh_is_current(entry.uid, entry.value, materials, lightmap))
      refresh_generated_geometry_mesh(entry.value, entry.uid, materials, lightmap);
  }
}

} // namespace client
