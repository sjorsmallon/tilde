#include "geometry_renderer.hpp"
#include "../shared/asset.hpp"
#include "../shared/color.hpp"
#include "../shared/log.hpp"
#include "render_assets.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace client
{

namespace
{

// Kinds whose mesh is GENERATED rather than loaded -- displacements and brushes
// -- live in the asset cache under a synthetic path keyed by uid, so one only
// gets rebuilt when it actually changes. The key is per OBJECT, not per kind: a
// uid names one object and one object has one generated mesh.
// Formats into CALLER-OWNED storage and returns a view of it -- no allocation.
//
// It used to return a std::string built as `"__geometry_" + std::to_string(uid)`,
// which is ~21 characters and therefore past the small-string buffer: one heap
// allocation per brush surface PER FRAME, immediately discarded. Paired with the
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

// The points each cached brush mesh was built from.
//
// A brush mesh is WORLD-space with an identity transform (a brush has no
// position member -- its vertices are its position), so the cache goes stale
// when a brush MOVES, not only when it changes shape. And a brush moves from the
// gizmo, from undo, from the inspector, from the map file -- asking every one of
// those to remember a refresh call is how the grey solid ends up sitting still
// while the contour lines walk away from it, which is exactly what happened.
//
// So the draw path checks instead of trusting. Same reasoning as the brush tool
// re-hulling every frame: a cache nothing can forget to invalidate.
std::unordered_map<shared::entity_uid_t, std::vector<linalg::vec3>>
    g_brush_mesh_source_points;

bool brush_mesh_is_current(shared::entity_uid_t uid, const std::vector<linalg::vec3> &vertices)
{
  const auto it = g_brush_mesh_source_points.find(uid);
  if (it == g_brush_mesh_source_points.end() || it->second.size() != vertices.size())
    return false;

  // Bit-exact, and element-wise because linalg::vec3 has no operator==. Anything
  // looser would let a sub-tolerance move leave the solid behind its contours.
  for (size_t i = 0; i < vertices.size(); ++i)
  {
    if (it->second[i].x != vertices[i].x || it->second[i].y != vertices[i].y ||
        it->second[i].z != vertices[i].z)
      return false;
  }
  return true;
}

// Empty for the kinds with no generated form; those never reach the cache.
assets::mesh_asset_t generate_geometry_mesh(const shared::geometry_value_t &geometry)
{
  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Displacement:
    return shared::generate_displacement_mesh(
        std::get<shared::displacement_geometry_t>(geometry));

  case shared::geometry_kind_t::Brush:
    return shared::generate_brush_mesh(std::get<shared::brush_geometry_t>(geometry));

  case shared::geometry_kind_t::Box:
  case shared::geometry_kind_t::Static_Mesh:
    return {};
  }

  log_error("generate_geometry_mesh: unhandled geometry kind {}",
            (int)shared::get_kind(geometry));
  return {};
}

renderer::pipeline_state_t state_for(const shared::geometry_surface_t &surface)
{
  renderer::pipeline_state_t state;
  state.shader = (surface.shader_type == "unlit") ? renderer::shader_t::unlit
                                                  : renderer::shader_t::lit;
  return state;
}

// The one texture a displacement surface wears. It used to have its own
// pipeline, descriptor pool, sampler and set (the whole g_disp_texture_* global
// cluster); a displacement is just a lit mesh with a texture, and now it says so.
//
// Returned as a one-slot TABLE rather than a handle, because that is the shape
// mesh_draw_t::material_overrides takes and the storage has to outlive the call.
Span<const renderer::material_handle_t> displacement_material_table()
{
  static const renderer::material_handle_t table[1] = {[] {
    renderer::material_t built{};
    built.parameters.base_color_texture = get_render_texture("resources/textures/dev_128x128.png");
    return renderer::register_material(built);
  }()};
  return Span<const renderer::material_handle_t>(table);
}

// Brushes are BLOCKOUT geometry, and a tiling dev texture is the wrong thing to
// read them by: it describes surface where what matters is SHAPE and SIZE. Flat
// light grey under shader_t::grid, which rules the world grid onto the faces --
// so a face reports its own dimensions, and the lines run on across a corner and
// on into the editor's floor grid.
//
// The grid costs no material parameter and no push-constant byte (the mesh block
// is at the 128-byte minimum with none left): generate_brush_mesh's UV is the
// world position projected on the face's dominant axis over the 128-unit cell,
// so the shader reads the grid straight out of fragUV.
//
// No texture handle at all -- an invalid one resolves to the internal 1x1 white
// through resolve_albedo_set, so this is base_color and nothing else. One shared
// material rather than one per surface colour: a brush is untinted at this stage,
// and a material per colour would be a material per brush.
Span<const renderer::material_handle_t> brush_material_table()
{
  static const renderer::material_handle_t table[1] = {[] {
    renderer::material_t built{};
    built.pipeline_state.shader = renderer::shader_t::grid;
    built.parameters.base_color = {0.72f, 0.72f, 0.75f, 1.0f};
    return renderer::register_material(built);
  }()};
  return Span<const renderer::material_handle_t>(table);
}

// Draw a surface's mesh if it resolves. False means "no mesh — use the kind's
// own primitive".
bool draw_surface_mesh(pass_builder_t &draws, const shared::geometry_surface_t &surface,
                       const linalg::vec3f &position, const linalg::vec3f &scale,
                       const linalg::vec3f &rotation)
{
  if (!surface.visible)
    return true; // resolved to "draw nothing", which is not a fallback case

  const renderer::mesh_handle_t mesh = get_render_mesh(shared::resolve_surface_mesh(surface));
  if (!mesh.valid())
    return false;

  renderer::mesh_draw_t draw{};
  draw.mesh      = mesh;
  draw.transform = linalg::compose_transform_euler(position, rotation, scale);

  if (surface.is_wireframe)
  {
    draw.fill = renderer::fill_mode_t::wireframe;
  }
  else
  {
    draw.tint               = color_from_vec3(surface.color);
    draw.material_overrides = material_variant(mesh, state_for(surface));
  }

  draws.meshes.push_back(draw);
  return true;
}

} // namespace

void draw_geometry(pass_builder_t &draws, const shared::geometry_value_t &geometry,
                   shared::entity_uid_t uid)
{
  const shared::geometry_surface_t &surface = shared::get_surface(geometry);

  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Box:
  {
    const shared::box_geometry_t &box = std::get<shared::box_geometry_t>(geometry);
    if (draw_surface_mesh(draws, surface, box.position, {1, 1, 1}, {0, 0, 0}))
      return;

    // No mesh: a box draws as a box.
    draws.debug.aabb(box.position - box.half_extents, box.position + box.half_extents,
                     color_from_vec3(surface.color), renderer::fill_mode_t::solid);
    return;
  }

  case shared::geometry_kind_t::Static_Mesh:
  {
    const shared::static_mesh_geometry_t &static_mesh =
        std::get<shared::static_mesh_geometry_t>(geometry);
    if (draw_surface_mesh(draws, surface, static_mesh.position, static_mesh.scale,
                          static_mesh.orientation))
      return;

    // A static mesh with no resolvable mesh has nothing to draw but its bound —
    // show it in yellow so a broken/missing asset is visible rather than absent.
    const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
    draws.debug.aabb(bounds.min, bounds.max, colors::yellow);
    return;
  }

  case shared::geometry_kind_t::Displacement:
  {
    const shared::displacement_geometry_t &displacement =
        std::get<shared::displacement_geometry_t>(geometry);

    // An explicit mesh_path overrides the generated grid, same as before.
    if (!surface.mesh_path.empty() &&
        draw_surface_mesh(draws, surface, displacement.position, {1, 1, 1}, {0, 0, 0}))
      return;

    if (!surface.visible)
      return;

    char                   cache_key_buffer[48];
    const std::string_view cache_key = generated_mesh_cache_key(uid, cache_key_buffer);
    assets::asset_handle_t<assets::mesh_asset_t> mesh_asset =
        assets::find_mesh_in_cache(cache_key);
    if (!mesh_asset.valid())
      mesh_asset = assets::register_dynamic_mesh(cache_key,
                                                 generate_geometry_mesh(geometry));

    const renderer::mesh_handle_t mesh = get_render_mesh(mesh_asset);
    if (!mesh.valid())
    {
      log_error("draw_geometry: could not build a mesh for displacement uid {}", uid);
      return;
    }

    // Its UVs come from the undisplaced world position / 128, baked in by
    // generate_displacement_mesh, so the texture tiles at 128 units and does not
    // swim while sculpting.
    renderer::mesh_draw_t draw{};
    draw.mesh               = mesh;
    draw.transform          = linalg::compose_transform_euler(displacement.position, {0, 0, 0},
                                                              {1, 1, 1});
    draw.material_overrides = displacement_material_table();
    draws.meshes.push_back(draw);
    return;
  }

  case shared::geometry_kind_t::Brush:
  {
    const shared::brush_geometry_t &brush = std::get<shared::brush_geometry_t>(geometry);

    if (!surface.mesh_path.empty() &&
        draw_surface_mesh(draws, surface, shared::get_position(geometry), {1, 1, 1},
                          {0, 0, 0}))
      return;

    if (!surface.visible)
      return;

    if (!brush_mesh_is_current(uid, brush.vertices))
      refresh_generated_geometry_mesh(geometry, uid);

    char                   cache_key_buffer[48];
    const std::string_view cache_key = generated_mesh_cache_key(uid, cache_key_buffer);
    assets::asset_handle_t<assets::mesh_asset_t> mesh_asset =
        assets::find_mesh_in_cache(cache_key);

    const renderer::mesh_handle_t mesh = get_render_mesh(mesh_asset);
    if (!mesh.valid())
    {
      log_error("draw_geometry: could not build a mesh for brush uid {}", uid);
      return;
    }

    // A brush holds WORLD-space points, so the generated mesh is already in
    // world space and the transform is identity. There is no position member to
    // put in one, which is the whole point: the vertices ARE the position.
    renderer::mesh_draw_t draw{};
    draw.mesh               = mesh;
    draw.transform          = linalg::compose_transform_euler({0, 0, 0}, {0, 0, 0},
                                                              {1, 1, 1});
    draw.material_overrides = brush_material_table();
    draws.meshes.push_back(draw);
    return;
  }
  }

  log_error("draw_geometry: unhandled geometry kind {}", (int)shared::get_kind(geometry));
}

void refresh_generated_geometry_mesh(const shared::geometry_value_t &geometry,
                                     shared::entity_uid_t uid)
{
  // Record what the mesh is about to be built from, so the draw path can tell
  // whether it still matches. Only brushes need it: every other generated kind
  // keeps its position in the transform and so does not go stale on a move.
  if (shared::get_kind(geometry) == shared::geometry_kind_t::Brush)
    g_brush_mesh_source_points[uid] =
        std::get<shared::brush_geometry_t>(geometry).vertices;

  char                   cache_key_buffer[48];
  const std::string_view cache_key = generated_mesh_cache_key(uid, cache_key_buffer);
  const assets::asset_handle_t<assets::mesh_asset_t> mesh_asset =
      assets::find_mesh_in_cache(cache_key);

  if (!mesh_asset.valid())
  {
    assets::register_dynamic_mesh(cache_key, generate_geometry_mesh(geometry));
    return;
  }

  assets::mesh_asset_t *mesh = assets::get_mutable(mesh_asset);
  if (!mesh)
  {
    log_error("refresh_generated_geometry_mesh: uid {} is cached but not mutable", uid);
    return;
  }

  *mesh = generate_geometry_mesh(geometry);

  // Eager re-upload, right here where the edit happened, rather than a flag the
  // next draw would have to notice.
  renderer::update_mesh(get_render_mesh(mesh_asset), *mesh);
}

} // namespace client
