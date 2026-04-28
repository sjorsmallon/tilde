#pragma once

#include "../game_state.hpp"
#include "../shader_tool_runtime.hpp"
#include "../../shared/file_watcher.hpp"
#include "../../shared/linalg.hpp"
#include "../camera.hpp"
#include "../../shared/asset.hpp"
#include <string>
#include <array>
#include <vector>

namespace client
{

struct editor_light_t
{
  linalg::vec3f position = {2.0f, 2.0f, 2.0f};
  linalg::vec3f direction = {0.0f, -1.0f, 0.0f};
  linalg::vec3f color = {1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = 20.0f;
  float spot_inner_degrees = 30.0f;
  float spot_outer_degrees = 45.0f;
  int light_type = 0; // 0=point, 1=spot, 2=directional
};

class ShaderEditorState : public IGameState
{
public:
  void on_enter() override;
  void on_exit() override;
  void update(float dt) override;
  void render_ui() override;
  void render_3d(VkCommandBuffer cmd) override;

private:
  void recompile_preview_shaders();
  void load_preview_mesh();

  // Camera (orbit mode)
  camera_t camera;

  // Mesh
  std::string mesh_path = "resources/obj/isosphere.obj";
  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;

  // PBR material
  std::string pbr_material_folder = "resources/textures/sloppy_mortar_stone";
  assets::asset_handle_t<assets::pbr_material_asset_t> pbr_material_handle;
  char pbr_folder_buffer[256] = {};

  // Configurable shader paths (default to preview shaders)
  std::string vert_shader_path;
  std::string frag_shader_path;
  char vert_path_buffer[256] = {};
  char frag_path_buffer[256] = {};

  // Lights
  std::vector<editor_light_t> lights;
  int selected_light_index = -1;

  // Shader compilation
  preview_pipeline_t preview_pipeline;
  std::string compile_error;
  bool pipeline_ready = false;
  float elapsed_time = 0.0f;

  // File watcher
  shared::File_Watcher file_watcher;

  // Parameter UI
  static constexpr int PARAM_COLOR_COUNT = 4;
  static constexpr int PARAM_VEC4_COUNT = 8;
  static constexpr int PARAM_FLOAT_COUNT = 16;

  std::array<float[4], PARAM_COLOR_COUNT> param_colors = {};
  std::array<float[4], PARAM_VEC4_COUNT> param_vec4s = {};
  std::array<float, PARAM_FLOAT_COUNT> param_floats = {};

  std::array<char[32], PARAM_COLOR_COUNT> param_color_labels = {};
  std::array<char[32], PARAM_VEC4_COUNT> param_vec4_labels = {};
  std::array<char[32], PARAM_FLOAT_COUNT> param_float_labels = {};

  // Controls
  bool invert_orbit_y = true;
  bool render_normals = false;

  // Light interaction
  bool mouse_was_down = false;
  bool dragging_light = false;

  // Path buffers for UI
  char mesh_path_buffer[256] = {};
};

} // namespace client
