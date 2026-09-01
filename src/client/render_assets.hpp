#pragma once

// The seam between the ASSET cache (paths and asset handles, shared with the
// server) and the RENDERER's own storage (GPU handles, client only). The
// renderer deliberately knows nothing about assets::asset_handle_t, and asset
// code knows nothing about the GPU; this is the one place that maps one to the
// other, and the one place that decides when an upload happens.
//
// Upload timing is the point. renderer::register_mesh blocks on a queue submit,
// so the first sight of a model must not be a draw call: preload_map_render_assets
// runs at map load and registers everything the map can show. The lazy path
// below is the safety net for anything a map could not name in advance.

#include "../shared/asset.hpp"
#include "../shared/map.hpp"
#include "renderer.hpp"

namespace client
{

// Registers on first request, then caches by asset index. Invalid in, invalid
// out -- the caller's draw is skipped and logged by the renderer.
renderer::mesh_handle_t get_render_mesh(assets::asset_handle_t<assets::mesh_asset_t> asset);

// A Render component's material as a pipeline_state. Only the shader varies
// today; blend, cull and depth are the material system's growth room. Shared
// rather than local because the editor preview has to resolve a material the
// same way the game does -- two spellings would be two ways for a model to look
// different in the editor than it does in play.
renderer::pipeline_state_t state_for(const entities::Material &material);

// The mesh's own materials re-registered under a different pipeline_state:
// the same textures and base colours, drawn unlit, or translucent, or with the
// depth test off. Cached per (mesh, state), so a per-frame call costs a lookup.
//
// The returned span is stable for the process and is meant to go straight into
// mesh_draw_t::material_overrides.
Span<const renderer::material_handle_t>
material_variant(renderer::mesh_handle_t mesh, const renderer::pipeline_state_t &state);

// Register every mesh and texture this map can show, up front. Called from the
// map-load tail on both the play and editor sides.
void preload_map_render_assets(const shared::map_t &map);

} // namespace client
