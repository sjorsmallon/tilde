// The editor tools must compile and LINK against pass_builder_t with no GPU.
//
// BUILD-ONLY: this target is built by the normal build but is not registered
// with ctest, so the check is the compile and the link. Running it across the
// DLL boundary would not test the tools -- game_shared and imgui are static
// libs linked into both this executable and game_client, so the DLL holds its
// own copies of the asset state and GImGui that only client::init fills, and
// making main() work would mean adding an exported hook per duplicated global.
// That is the module layout's problem, not the editor's.
//
// The original statement of intent:
//
// That used to need a MockRenderer implementing a five-method virtual
// interface. It needs nothing now: a tool draws by appending to a
// pass_builder_t, and filling vectors needs no device, no swapchain and no
// command buffer. The mock is gone because the thing it was mocking is gone.
//
// Scope, deliberately: construction and on_draw_overlay, which is the seam this
// covers. The input handlers are NOT driven here -- several read ImGui state,
// and imgui and game_shared are static libs linked into both this executable
// and game_client, so the DLL holds its own GImGui that only client::init
// fills. Driving them across that boundary would test the module layout rather
// than the tools.

// renderer.hpp pulls in SDL.h, which rewrites `main` unless told not to. This
// test owns its own entry point; only the launchers want SDL's shim.
#define SDL_MAIN_HANDLED

#include "../../shared/editor_grid.hpp"
#include "../frame_builder.hpp"
#include "editor_tool.hpp"
#include "editor_bvh.hpp"
#include "editor_types.hpp"
#include "transaction_system.hpp"
#include "tools/animation_tool.hpp"
#include "tools/displacement_tool.hpp"
#include "tools/particle_editor_tool.hpp"
#include "tools/pathfinding_test_tool.hpp"
#include "tools/placement_tool.hpp"
#include "tools/sculpting_tool.hpp"
#include "tools/selection_tool.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

int main()
{
  // A real context, not a zeroed one: the editor never runs a tool without a
  // map, a BVH, a transaction system and a grid, and some of them assert on it.
  shared::map_t map;
  {
    shared::box_geometry_t floor;
    floor.position     = {0, -8, 0};
    floor.half_extents = {256, 8, 256};
    map.geometry.push_back({map.next_uid++, floor});
  }

  const Bounding_Volume_Hierarchy bvh = client::build_editor_bvh(map);
  client::Transaction_System      transactions;
  editor::grid_settings_t         grid;
  bool                            geometry_updated = false;

  client::editor_context_t context = {};
  context.map                = &map;
  context.bvh                = &bvh;
  context.transaction_system = &transactions;
  context.grid               = &grid;
  context.geometry_updated_so_bvh_rebuild_is_needed = &geometry_updated;

  client::pass_builder_t draws;

  std::vector<std::unique_ptr<client::Editor_Tool>> tools;
  tools.push_back(std::make_unique<client::Selection_Tool>());
  tools.push_back(std::make_unique<client::Placement_Tool>());
  tools.push_back(std::make_unique<client::Sculpting_Tool>());
  tools.push_back(std::make_unique<client::Pathfinding_Test_Tool>());
  tools.push_back(std::make_unique<client::Particle_Editor_Tool>());
  tools.push_back(std::make_unique<client::Displacement_Tool>());
  tools.push_back(std::make_unique<client::Animation_Tool>());

  for (const std::unique_ptr<client::Editor_Tool> &tool : tools)
  {
    tool->on_enable(context);
    tool->on_draw_overlay(context, draws);
    tool->on_disable(context);
  }

  // Nothing above touched the GPU, so no tool can have registered a mesh --
  // every draw a tool made went into the debug list.
  assert(draws.meshes.empty());

  // And whatever they appended, the frame maintenance has to survive it.
  draws.begin_frame(1.0f / 60.0f);

  std::cout << "editor_compile_test passed (" << tools.size() << " tools)" << std::endl;
  return 0;
}
