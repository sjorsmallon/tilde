#pragma once

#include "../frame_builder.hpp"
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

enum class debug_flag : int32_t
{
  RenderNormals = (1 << 0),
  RenderUV = (1 << 1),
  RenderParallaxUV = (1 << 2)
};

struct editor_light_t
{
  linalg::vec3f position = {2.0f, 2.0f, 2.0f};
  linalg::vec3f direction = {0.0f, -1.0f, 0.0f};
  linalg::vec3f color = {1.0f, 1.0f, 1.0f};
  // The SAME unit as entities::Light::intensity: the irradiance this light
  // delivers at LIGHT_REFERENCE_DISTANCE. The preview folds it through
  // shared::radiance_of like the game and the bake do, so a number that looks
  // right here is the number to type into the map editor. It used to be 1500,
  // because pbr.frag multiplied colour by intensity raw -- a second unit for one
  // field, which is what lighting_def.md ss11 is about.
  float intensity = 1.0f;
  // Range is the cutoff radius (world units) where the windowed falloff reaches zero.
  float range = 512.0f;
  float spot_inner_degrees = 30.0f;
  float spot_outer_degrees = 45.0f;
  // The emitter's radius, in the same world units as `range`, and it is here for
  // the reason `intensity` is in the same unit as the game's: a preview whose
  // highlights are a different SHAPE from the game's is a tool that lies about
  // the shader you are authoring in it. Directional lights in this tool have no
  // angular diameter control -- the map editor is where a sun is authored.
  float source_radius = 0.0f;
  int light_type = 0; // 0=point, 1=spot, 2=directional
};

class Shader_Editor_State : public Game_State
{
public:
  void on_enter() override;
  void on_exit() override;
  void update(float dt) override;
  void draw_imgui_panels() override;
  void build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes,
                   renderer::ui_draw_list_t &ui) override;

private:
  // Recorded inside the render pass by render_frame. Static because
  // custom_draw_t carries a plain function pointer and a void* -- no virtuals,
  // no capture, nothing that could outlive the state.
  static void record_preview_draw(VkCommandBuffer cmd, void *user);

  // This frame's view pass and the buffers the custom draw reads.
  pass_builder_t            scene;
  renderer::mesh_gpu_info_t preview_mesh{};


private:
  void recompile_preview_shaders();
  void load_preview_mesh();

  // Camera (orbit mode)
  camera_t camera;

  // Mesh
  std::string mesh_path = "resources/obj/Isosphere.obj";
  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;

  // sloppy_mortar_stone
  // PBR material
  std::string pbr_material_folder = "resources/textures/harsh_bricks";
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
  bool render_uv = false;
  bool render_parallax_uv = false;

  // Light interaction
  bool mouse_was_down = false;
  bool dragging_light = false;

  // Path buffers for UI
  char mesh_path_buffer[256] = {};
};

} // namespace client
