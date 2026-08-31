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

// Kinds whose mesh is GENERATED rather than loaded -- brushes
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

// What each cached brush mesh was built from.
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
//
// It holds the whole geometry VALUE rather than just the point set, because a
// face's material is now part of what the mesh was built from and changing one
// moves no vertex. And the material TABLE beside it, because an index resolves
// against that table: retexturing entry 3 changes the mesh of every brush whose
// faces name it, with nothing about the brushes themselves different.
struct generated_brush_source_t
{
  shared::geometry_value_t value;
  std::vector<std::string> materials;
  // The bake the UVs came from. A rebake moves charts in the atlas without
  // moving a vertex or changing a material, so neither of the two above can
  // notice one.
  uint32_t lightmap_geometry_id = 0;
  // Per submesh slot. Rebuilt with the mesh, so the two cannot describe
  // different brushes.
  std::vector<renderer::material_handle_t> overrides;
};

std::unordered_map<shared::entity_uid_t, generated_brush_source_t> g_brush_mesh_sources;

bool brush_mesh_is_current(shared::entity_uid_t uid,
                           const shared::geometry_value_t &geometry,
                           Span<const std::string> materials,
                           const shared::lightmap_t &lightmap)
{
  const auto it = g_brush_mesh_sources.find(uid);
  if (it == g_brush_mesh_sources.end())
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

// Empty for the kinds with no generated form; those never reach the cache.
assets::mesh_asset_t generate_geometry_mesh(const shared::geometry_value_t &geometry,
                                            shared::entity_uid_t uid,
                                            Span<const std::string> materials,
                                            const shared::lightmap_t &lightmap)
{
  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Brush:
    return shared::generate_brush_mesh(std::get<shared::brush_geometry_t>(geometry),
                                       materials, {&lightmap, uid});

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

// An UNTEXTURED brush face is BLOCKOUT geometry, and a tiling dev texture is the
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
// through resolve_albedo_set, so this is base_color and nothing else.
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

// One material per distinct TEXTURE, not per submesh and not per rebuild: a
// brush mesh is re-registered on every edit, and registering a fresh material
// each time would grow the renderer's table for the length of an editing
// session. Keyed by the texture handle because that is the whole difference
// between two textured faces here.
renderer::material_handle_t textured_face_material(renderer::texture_handle_t texture)
{
  static std::unordered_map<uint32_t, renderer::material_handle_t> by_texture;

  const auto cached = by_texture.find(texture.index);
  if (cached != by_texture.end())
    return cached->second;

  renderer::material_t built{};
  built.parameters.base_color_texture = texture;
  const renderer::material_handle_t handle = renderer::register_material(built);
  by_texture.emplace(texture.index, handle);
  return handle;
}

// A face painted between two materials. Keyed by the pair, for
// textured_face_material's reason: the mesh is re-registered on every edit and
// a fresh material per rebuild would grow the renderer's table for the length
// of an editing session.
renderer::material_handle_t blended_face_material(const renderer::material_parameters_t &parameters)
{
  struct key_t
  {
    uint32_t base = 0;
    uint32_t layers[BLEND_LAYER_COUNT - 1] = {};

    bool operator==(const key_t &) const = default;
  };
  struct key_hash_t
  {
    size_t operator()(const key_t &key) const
    {
      size_t hash = key.base;
      for (uint32_t layer : key.layers)
        hash = hash * 1099511628211ull + layer;
      return hash;
    }
  };

  static std::unordered_map<key_t, renderer::material_handle_t, key_hash_t> by_textures;

  key_t key;
  key.base = parameters.base_color_texture.index;
  for (int layer = 1; layer < BLEND_LAYER_COUNT; ++layer)
    key.layers[layer - 1] = parameters.blend_textures.data[layer - 1].index;

  const auto cached = by_textures.find(key);
  if (cached != by_textures.end())
    return cached->second;

  renderer::material_t built{};
  built.pipeline_state.shader = renderer::shader_t::blend;
  built.parameters.base_color_texture = parameters.base_color_texture;
  built.parameters.blend_textures = parameters.blend_textures;

  const renderer::material_handle_t handle = renderer::register_material(built);
  by_textures.emplace(key, handle);
  return handle;
}

// The override table for a generated brush mesh: one entry per submesh slot,
// grid-shaded where the face's material resolved to no texture and lit where it
// did. Built from the mesh's OWN materials, which generate_brush_mesh already
// filled with the resolved texture for each slot -- so a slot naming a texture
// that failed to load carries the renderer's magenta checkerboard and is visible
// rather than silently flat.
//
// Stored beside the source record so it is rebuilt exactly when the mesh is, and
// the two cannot describe different brushes.
std::vector<renderer::material_handle_t>
build_brush_material_overrides(renderer::mesh_handle_t mesh)
{
  std::vector<renderer::material_handle_t> table;
  for (renderer::material_handle_t slot : renderer::mesh_default_materials(mesh))
  {
    const renderer::material_parameters_t parameters = renderer::material_parameters(slot);

    if (parameters.blend_textures.data[0].valid())
      table.push_back(blended_face_material(parameters));
    else
      table.push_back(parameters.base_color_texture.valid()
                          ? textured_face_material(parameters.base_color_texture)
                          : blockout_material());
  }
  return table;
}

// Draw a surface's mesh if it resolves. False means "no mesh — use the kind's
// own primitive".
bool draw_surface_mesh(pass_builder_t &draws, const shared::geometry_surface_t &surface,
                       const linalg::vec3f& position, const linalg::vec3f& scale,
                       const linalg::quatf& rotation)
{
  if (!surface.visible)
    return true; // resolved to "draw nothing", which is not a fallback case

  const renderer::mesh_handle_t mesh = get_render_mesh(shared::resolve_surface_mesh(surface));
  if (!mesh.valid())
    return false;

  renderer::mesh_draw_t draw{};
  draw.mesh      = mesh;
  draw.transform = linalg::compose_transform(position, rotation, scale);

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
                   shared::entity_uid_t uid, Span<const std::string> materials,
                   const shared::lightmap_t &lightmap)
{
  const shared::geometry_surface_t &surface = shared::get_surface(geometry);

  switch (shared::get_kind(geometry))
  {
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

  case shared::geometry_kind_t::Brush:
  {
    const shared::brush_geometry_t &brush = std::get<shared::brush_geometry_t>(geometry);

    if (!surface.mesh_path.empty() &&
        draw_surface_mesh(draws, surface, shared::get_position(geometry), {1, 1, 1},
                          {0, 0, 0}))
      return;

    if (!surface.visible)
      return;

    if (!brush_mesh_is_current(uid, geometry, materials, lightmap))
      refresh_generated_geometry_mesh(geometry, uid, materials, lightmap);

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
    draw.mesh      = mesh;
    draw.transform = linalg::mat4f::identity();

    const auto source = g_brush_mesh_sources.find(uid);
    if (source != g_brush_mesh_sources.end())
      draw.material_overrides = source->second.overrides;

    draws.meshes.push_back(draw);
    return;
  }
  }

  log_error("draw_geometry: unhandled geometry kind {}", (int)shared::get_kind(geometry));
}

void refresh_generated_geometry_mesh(const shared::geometry_value_t &geometry,
                                     shared::entity_uid_t uid,
                                     Span<const std::string> materials,
                                     const shared::lightmap_t &lightmap)
{
  const bool is_brush = shared::get_kind(geometry) == shared::geometry_kind_t::Brush;

  // Record what the mesh is about to be built from, so the draw path can tell
  // whether it still matches. Only brushes need it: every other generated kind
  // keeps its position in the transform and so does not go stale on a move.
  if (is_brush)
  {
    generated_brush_source_t &source = g_brush_mesh_sources[uid];
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
    mesh_asset =
        assets::register_dynamic_mesh(cache_key,
                                      generate_geometry_mesh(geometry, uid, materials, lightmap));
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
  }

  // The slot table follows the mesh it describes -- a rebuild can change how
  // many submeshes there are, so a stale table would name the wrong material for
  // every slot after the one that moved.
  if (is_brush)
    g_brush_mesh_sources[uid].overrides =
        build_brush_material_overrides(get_render_mesh(mesh_asset));
}

} // namespace client
