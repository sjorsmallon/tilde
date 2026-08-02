#include "shader_editor_state.hpp"
#include "../input.hpp"
#include "../renderer.hpp"
#include "../state_manager.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/old_ideas/ecs.hpp"
#include "log.hpp"
#include "shader_tool_paths.h"
#include "imgui.h"
#include <SDL.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <print>

namespace client
{

// Must match the UBO struct in shader_tool_runtime.cpp and shader_tool_common.glsl
static constexpr int MAX_LIGHTS = 8;

struct gpu_light_t
{
  float position[4];
  float direction[4];
  float color_intensity[4];
  float spot_params[4];
};

struct preview_scene_ubo_t
{
  float view[16];
  float projection[16];
  float view_projection[16];
  float camera_position[4];
  float time[4];
  int32_t light_count;
  int32_t debug_flags, _pad1, _pad2;
  gpu_light_t lights[MAX_LIGHTS];
  float param_color[4][4];
  float param_vec4[8][4];
  float param_float[16];
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Shader_Editor_State::on_enter()
{
  log_terminal("[ShaderEditor] Entering shader editor state");

  // Initialize camera in orbit mode
  camera = {};
  camera.orbit = true;
  camera.orbit_target = {0.0f, 0.0f, 0.0f};
  camera.orbit_distance = 200.0f;
  camera.orbit_max_distance = 2000.0f;
  camera.yaw = 45.0f;
  camera.pitch = 25.0f;
  update_orbit(camera);

  // Default light
  lights.clear();
  editor_light_t default_light;
  default_light.position = {96.0f, 128.0f, 64.0f};
  default_light.direction = linalg::normalize(linalg::vec3f{0.0f, 0.0f, 0.0f} - linalg::vec3f{96.0f, 128.0f, 64.0f});
  default_light.color = {1.0f, 0.95f, 0.9f};
  // Sphere sits near origin, light at d ≈ 172 units. With 1/d² falloff intensity must
  // be on the order of d² to give ~unit brightness at the surface — see editor_light_t.
  default_light.intensity = 30000.0f;
  default_light.range = 512.0f;
  default_light.light_type = 1; // spot
  lights.push_back(default_light);
  selected_light_index = 0;

  // Init parameter labels
  for (int i = 0; i < PARAM_COLOR_COUNT; i++)
    snprintf(param_color_labels[i], 32, "color_%d", i);
  for (int i = 0; i < PARAM_VEC4_COUNT; i++)
    snprintf(param_vec4_labels[i], 32, "vec4_%d", i);
  for (int i = 0; i < PARAM_FLOAT_COUNT; i++)
    snprintf(param_float_labels[i], 32, "float_%d", i);

  // Init param values
  param_colors = {};
  param_vec4s = {};
  param_floats = {};
  // Default clay color in slot 0
  param_colors[0][0] = 0.8f;
  param_colors[0][1] = 0.75f;
  param_colors[0][2] = 0.7f;
  param_colors[0][3] = 1.0f;

  // Mesh path
  mesh_path = "resources/obj/isosphere.obj";
  strncpy(mesh_path_buffer, mesh_path.c_str(), sizeof(mesh_path_buffer) - 1);

  // Shader paths — default to PBR shaders
  {
    std::filesystem::path pbr_dir = std::filesystem::path(PREVIEW_SHADER_DIR).parent_path() / "pbr";
    vert_shader_path = (pbr_dir / "pbr.vert").string();
    frag_shader_path = (pbr_dir / "pbr.frag").string();
  }
  strncpy(vert_path_buffer, vert_shader_path.c_str(), sizeof(vert_path_buffer) - 1);
  strncpy(frag_path_buffer, frag_shader_path.c_str(), sizeof(frag_path_buffer) - 1);

  // PBR material folder
  strncpy(pbr_folder_buffer, pbr_material_folder.c_str(), sizeof(pbr_folder_buffer) - 1);

  // Load mesh
  load_preview_mesh();

  // Create Vulkan resources (descriptor set, UBO, pipeline layout)
  VkDevice device = renderer::GetDevice();
  VkPhysicalDevice physical_device = renderer::GetPhysicalDevice();

  if (!create_preview_resources(device, physical_device, preview_pipeline))
  {
    log_error("[ShaderEditor] Failed to create preview Vulkan resources");
    return;
  }

  // Auto-load PBR material and bind textures
  pbr_material_handle = assets::load_pbr_material(pbr_material_folder.c_str());
  if (pbr_material_handle.valid())
  {
    const assets::pbr_material_asset_t *mat = assets::get(pbr_material_handle);
    if (mat)
      bind_pbr_textures(device, preview_pipeline, *mat);
  }

  // Compile shaders and create pipeline
  recompile_preview_shaders();

  // Set up file watcher — watch user-specified shader files + common header
  std::string shader_dir = PREVIEW_SHADER_DIR;
  auto reload_callback = [this](const std::filesystem::path &)
  {
    log_terminal("[ShaderEditor] Shader file changed, recompiling...");
    recompile_preview_shaders();
  };

  file_watcher.add_file(std::filesystem::path(vert_shader_path), reload_callback);
  file_watcher.add_file(std::filesystem::path(frag_shader_path), reload_callback);
  // Always watch shader_tool_common.glsl since most shaders #include it
  file_watcher.add_file(std::filesystem::path(shader_dir) / "shader_tool_common.glsl",
                        reload_callback);

  elapsed_time = 0.0f;
  log_terminal("[ShaderEditor] Initialization complete");
}

void Shader_Editor_State::on_exit()
{
  log_terminal("[ShaderEditor] Exiting shader editor state");
  VkDevice device = renderer::GetDevice();
  vkDeviceWaitIdle(device);
  destroy_preview_resources(device, preview_pipeline);
  pipeline_ready = false;
  lights.clear();
  mesh_handle = {};
}

// ---------------------------------------------------------------------------
// Shader compilation
// ---------------------------------------------------------------------------

void Shader_Editor_State::recompile_preview_shaders()
{
  std::string shader_dir = std::filesystem::path(PREVIEW_SHADER_DIR).parent_path().string();
  std::string cache_dir  = SHADER_TOOL_CACHE_DIR;
  std::string vert_src   = vert_shader_path;
  std::string frag_src   = frag_shader_path;
  std::string vert_spv   = cache_dir + "/preview.vert.spv";
  std::string frag_spv   = cache_dir + "/preview.frag.spv";

  std::string error;

  if (!compile_shader_to_spv(vert_src, vert_spv, shader_dir, error))
  {
    compile_error = "Vertex shader error:\n" + error;
    log_warning("[ShaderEditor] Vertex shader compilation failed");
    return;
  }

  if (!compile_shader_to_spv(frag_src, frag_spv, shader_dir, error))
  {
    compile_error = "Fragment shader error:\n" + error;
    log_warning("[ShaderEditor] Fragment shader compilation failed");
    return;
  }

  VkDevice device = renderer::GetDevice();
  vkDeviceWaitIdle(device);

  if (create_preview_pipeline_from_spv(device, renderer::GetRenderPass(),
                                       vert_spv, frag_spv, preview_pipeline))
  {
    pipeline_ready = true;
    compile_error.clear();
    log_terminal("[ShaderEditor] Shaders compiled and pipeline created OK");
  }
  else
  {
    compile_error = "Failed to create Vulkan pipeline from SPIR-V";
    log_error("[ShaderEditor] Pipeline creation failed");
  }
}

// ---------------------------------------------------------------------------
// Mesh loading
// ---------------------------------------------------------------------------

void Shader_Editor_State::load_preview_mesh()
{
  mesh_handle = assets::load_mesh(mesh_path.c_str());
  if (!mesh_handle.valid())
  {
    log_error("[ShaderEditor] Failed to load mesh: {}", mesh_path);
  }
  else
  {
    log_terminal("[ShaderEditor] Loaded mesh: {}", mesh_path);
  }
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void Shader_Editor_State::update(float dt)
{
  elapsed_time += dt;

  // File watcher for hot reload
  file_watcher.update();

  // Escape to return to main menu
  if (input::is_key_pressed(input::key_t::Escape))
  {
    state_manager::switch_to(game_state::main_menu);
    return;
  }

  // Camera controls (only when ImGui doesn't want the mouse)
  if (!input::ui_wants_mouse())
  {
    // RMB drag = orbit
    if (input::is_mouse_down(input::mouse_button_t::Right))
    {
      linalg::vec2i delta = input::mouse_delta();
      float dy = static_cast<float>(delta.y) * (invert_orbit_y ? -1.0f : 1.0f);
      orbit_rotate(camera, static_cast<float>(delta.x), dy);
    }

    // MMB drag = pan
    if (input::is_mouse_down(input::mouse_button_t::Middle))
    {
      linalg::vec2i delta = input::mouse_delta();
      orbit_pan(camera, static_cast<float>(delta.x),
                static_cast<float>(delta.y));
    }

    // Scroll = zoom
    float scroll = input::scroll_delta();
    if (scroll != 0.0f)
    {
      orbit_zoom(camera, scroll, 16.0f);
    }

    // LMB: select light / drag along facing direction
    bool mouse_down = input::is_mouse_down(input::mouse_button_t::Left);
    bool mouse_just_pressed = mouse_down && !mouse_was_down;

    if (mouse_just_pressed)
    {
      linalg::vec2i mouse = input::mouse_position();

      SDL_Window *window = SDL_GL_GetCurrentWindow();
      int window_width = 1, window_height = 1;
      if (window)
        SDL_GetWindowSize(window, &window_width, &window_height);

      float ndc_x = 2.0f * mouse.x / window_width - 1.0f;
      float ndc_y = 1.0f - 2.0f * mouse.y / window_height;
      float aspect_ratio = static_cast<float>(window_width) /
                           static_cast<float>(window_height);

      camera_t pick_camera = camera;
      if (pick_camera.orbit)
        look_at(pick_camera, pick_camera.orbit_target);

      linalg::ray_t ray =
          get_pick_ray(pick_camera, ndc_x, ndc_y, aspect_ratio);

      // Test against a sphere around each light position
      constexpr float pick_radius = 12.0f;
      float closest_t = 1e30f;
      int hit_index = -1;

      for (int li = 0; li < static_cast<int>(lights.size()); li++)
      {
        linalg::vec3f to_light = lights[li].position - ray.origin;
        float t_along_ray = linalg::dot(to_light, ray.direction);
        if (t_along_ray < 0.0f)
          continue;

        linalg::vec3f nearest_point =
            ray.origin + ray.direction * t_along_ray;
        float distance =
            linalg::length(nearest_point - lights[li].position);
        if (distance <= pick_radius && t_along_ray < closest_t)
        {
          closest_t = t_along_ray;
          hit_index = li;
        }
      }

      if (hit_index >= 0)
      {
        selected_light_index = hit_index;
        dragging_light = true;
      }
      else
      {
        dragging_light = false;
      }
    }

    // Move selected light along its direction while dragging
    if (dragging_light && mouse_down && !mouse_just_pressed &&
        selected_light_index >= 0 &&
        selected_light_index < static_cast<int>(lights.size()))
    {
      linalg::vec2i delta = input::mouse_delta();

      auto &light = lights[selected_light_index];
      float direction_length = linalg::length(light.direction);
      if (direction_length > 0.001f)
      {
        linalg::vec3f normalized_direction =
            light.direction * (1.0f / direction_length);
        float camera_distance =
            linalg::length(light.position - camera.position);
        float sensitivity = camera_distance * 0.005f;
        float move_amount = static_cast<float>(-delta.y) * sensitivity;
        light.position =
            light.position + normalized_direction * move_amount;
      }
    }

    if (!mouse_down)
    {
      dragging_light = false;
    }
  }
  else
  {
    dragging_light = false;
  }
  mouse_was_down = input::is_mouse_down(input::mouse_button_t::Left);
}

// ---------------------------------------------------------------------------
// 3D Rendering
// ---------------------------------------------------------------------------

void Shader_Editor_State::render_3d(VkCommandBuffer cmd)
{
  // In orbit mode, yaw/pitch encode the offset direction (target → camera),
  // so get_orientation_vectors returns a forward that points AWAY from the
  // target.  Fix this by recomputing yaw/pitch to look at the orbit target.
  camera_t render_camera = camera;
  if (render_camera.orbit)
  {
    look_at(render_camera, render_camera.orbit_target);
  }

  // Set up render_view so draw_line etc. use the correct VP matrix
  renderer::render_view_t view_def;
  view_def.viewport = {{0, 0}, {1, 1}};
  view_def.camera = render_camera;
  ecs::Registry reg;
  renderer::render_view(cmd, view_def, reg);
  renderer::set_viewport(cmd, view_def.viewport);

  if (!pipeline_ready || preview_pipeline.pipeline == VK_NULL_HANDLE)
    return;

  if (!mesh_handle.valid())
    return;

  renderer::mesh_gpu_info_t mesh_info;
  if (!renderer::GetMeshGPUInfo(mesh_handle, mesh_info))
    return;

  // Build UBO data — reconstruct view/proj from camera the same way
  // render_view does, but using linalg::mat4f for the UBO layout.
  auto [forward, right, up] = get_orientation_vectors(render_camera);
  linalg::vec3f eye = render_camera.position;

  // Build view matrix (look-at from position along forward)
  linalg::vec3f target_point = eye + forward;
  linalg::vec3f f = linalg::normalize(target_point - eye);
  linalg::vec3f r = linalg::normalize(linalg::cross(f, linalg::vec3f{0, 1, 0}));
  linalg::vec3f u = linalg::cross(r, f);

  linalg::mat4f view = linalg::mat4f::identity();
  view[0] = {r.x, u.x, -f.x, 0.0f};
  view[1] = {r.y, u.y, -f.y, 0.0f};
  view[2] = {r.z, u.z, -f.z, 0.0f};
  view[3] = {-linalg::dot(r, eye), -linalg::dot(u, eye),
             linalg::dot(f, eye), 1.0f};

  // Projection — match render_view's perspective (90 deg fov, Vulkan Y-flip)
  float fov_rad = 1.5708f; // 90 degrees
  float aspect = 16.0f / 9.0f;
  {
    int window_width, window_height;
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    if (window)
    {
      SDL_GetWindowSize(window, &window_width, &window_height);
      if (window_height > 0)
        aspect = static_cast<float>(window_width) /
                 static_cast<float>(window_height);
    }
  }
  float tan_half = std::tan(fov_rad * 0.5f);
  linalg::mat4f projection = {};
  projection[0] = {1.0f / (aspect * tan_half), 0.0f, 0.0f, 0.0f};
  projection[1] = {0.0f, -1.0f / tan_half, 0.0f, 0.0f}; // Vulkan Y-flip
  projection[2] = {0.0f, 0.0f, 50000.0f / (1.0f - 50000.0f), -1.0f};
  projection[3] = {0.0f, 0.0f, (1.0f * 50000.0f) / (1.0f - 50000.0f), 0.0f};

  linalg::mat4f view_projection = projection * view;

  preview_scene_ubo_t ubo = {};
  memcpy(ubo.view, &view, sizeof(float) * 16);
  memcpy(ubo.projection, &projection, sizeof(float) * 16);
  memcpy(ubo.view_projection, &view_projection, sizeof(float) * 16);
  ubo.camera_position[0] = eye.x;
  ubo.camera_position[1] = eye.y;
  ubo.camera_position[2] = eye.z;
  ubo.time[0] = elapsed_time;
  ubo.time[1] = std::sin(elapsed_time);
  ubo.time[2] = std::cos(elapsed_time);
  ubo.time[3] = 0.016f; // approximate dt

  ubo.light_count  = static_cast<int32_t>(std::min(lights.size(), static_cast<size_t>(MAX_LIGHTS)));
  ubo.debug_flags  = 0;
  if (render_normals) ubo.debug_flags |= static_cast<int32_t>(debug_flag::RenderNormals);
  if (render_uv) ubo.debug_flags |= static_cast<int32_t>(debug_flag::RenderUV);
  if (render_parallax_uv) ubo.debug_flags |= static_cast<int32_t>(debug_flag::RenderParallaxUV);
  for (int i = 0; i < ubo.light_count; i++)
  {
    const auto &light = lights[i];
    ubo.lights[i].position[0] = light.position.x;
    ubo.lights[i].position[1] = light.position.y;
    ubo.lights[i].position[2] = light.position.z;
    ubo.lights[i].direction[0] = light.direction.x;
    ubo.lights[i].direction[1] = light.direction.y;
    ubo.lights[i].direction[2] = light.direction.z;
    ubo.lights[i].color_intensity[0] = light.color.x;
    ubo.lights[i].color_intensity[1] = light.color.y;
    ubo.lights[i].color_intensity[2] = light.color.z;
    ubo.lights[i].color_intensity[3] = light.intensity;
    ubo.lights[i].spot_params[0] =
        std::cos(linalg::to_radians(light.spot_inner_degrees));
    ubo.lights[i].spot_params[1] =
        std::cos(linalg::to_radians(light.spot_outer_degrees));
    ubo.lights[i].spot_params[2] = light.range;
    ubo.lights[i].spot_params[3] = static_cast<float>(light.light_type);
  }

  // Copy parameter slots
  for (int i = 0; i < PARAM_COLOR_COUNT; i++)
    memcpy(ubo.param_color[i], param_colors[i], sizeof(float) * 4);
  for (int i = 0; i < PARAM_VEC4_COUNT; i++)
    memcpy(ubo.param_vec4[i], param_vec4s[i], sizeof(float) * 4);
  memcpy(ubo.param_float, param_floats.data(), sizeof(float) * PARAM_FLOAT_COUNT);

  // Upload UBO to the current frame's buffer to avoid GPU/CPU races.
  uint32_t frame = renderer::GetCurrentFrame();
  memcpy(preview_pipeline.ubo_mapped[frame], &ubo, sizeof(ubo));

  // Bind pipeline
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    preview_pipeline.pipeline);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          preview_pipeline.pipeline_layout, 0, 1,
                          &preview_pipeline.descriptor_set[frame], 0, nullptr);

  // Push constant: identity model matrix (OBJ loader normalizes to 100 units)
  float model_matrix[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  vkCmdPushConstants(cmd, preview_pipeline.pipeline_layout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(model_matrix),
                     model_matrix);

  // Draw mesh
  VkBuffer vertex_buffers[] = {mesh_info.vertex_buffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(cmd, 0, 1, vertex_buffers, offsets);
  vkCmdBindIndexBuffer(cmd, mesh_info.index_buffer, 0, VK_INDEX_TYPE_UINT32);
  vkCmdDrawIndexed(cmd, mesh_info.index_count, 1, 0, 0, 0);

  // Ground grid (Y=0 plane)
  {
    constexpr int half_extent = 8;
    constexpr float step = 32.0f;
    color_t grid_color = colors::grey;
    color_t axis_x_color = color_t{204, 68, 68}; // red-ish for X
    color_t axis_z_color = color_t{68, 204, 68}; // green-ish for Z

    for (int i = -half_extent; i <= half_extent; i++)
    {
      float offset = static_cast<float>(i) * step;
      color_t color = (i == 0) ? axis_x_color : grid_color;
      renderer::draw_line(cmd,
                         {static_cast<float>(-half_extent) * step, 0.0f, offset},
                         {static_cast<float>(half_extent) * step, 0.0f, offset},
                         color);
      color = (i == 0) ? axis_z_color : grid_color;
      renderer::draw_line(cmd,
                         {offset, 0.0f, static_cast<float>(-half_extent) * step},
                         {offset, 0.0f, static_cast<float>(half_extent) * step},
                         color);
    }
  }

  // Draw light position indicators using renderer debug lines
  for (int i = 0; i < static_cast<int>(lights.size()); i++)
  {
    const auto &light = lights[i];
    color_t light_color =
        (i == selected_light_index) ? colors::yellow : colors::green;

    float size = 8.0f;
    linalg::vec3f p = light.position;
    renderer::draw_line(cmd, {p.x - size, p.y, p.z},
                       {p.x + size, p.y, p.z}, light_color);
    renderer::draw_line(cmd, {p.x, p.y - size, p.z},
                       {p.x, p.y + size, p.z}, light_color);
    renderer::draw_line(cmd, {p.x, p.y, p.z - size},
                       {p.x, p.y, p.z + size}, light_color);

    // Draw frustum / direction visualization
    float direction_length = linalg::length(light.direction);
    if (direction_length > 0.001f)
    {
      linalg::vec3f normalized_direction =
          light.direction * (1.0f / direction_length);

      if (light.light_type == 1) // Spot light: cone frustum
      {
        // Build perpendicular basis from direction
        linalg::vec3f arbitrary =
            (std::abs(normalized_direction.y) < 0.99f)
                ? linalg::vec3f{0, 1, 0}
                : linalg::vec3f{1, 0, 0};
        linalg::vec3f right_axis = linalg::normalize(
            linalg::cross(normalized_direction, arbitrary));
        linalg::vec3f up_axis =
            linalg::cross(right_axis, normalized_direction);

        linalg::vec3f base_center =
            p + normalized_direction * light.range;

        float clamped_outer =
            std::min(light.spot_outer_degrees, 89.0f);
        float outer_radius =
            light.range *
            std::tan(linalg::to_radians(clamped_outer));

        constexpr int segments = 32;
        constexpr float two_pi = 6.2831853f;

        color_t outer_color = light_color;

        for (int s = 0; s < segments; s++)
        {
          float angle_current = two_pi * s / segments;
          float angle_next = two_pi * (s + 1) / segments;

          linalg::vec3f point_current =
              base_center +
              right_axis *
                  (std::cos(angle_current) * outer_radius) +
              up_axis *
                  (std::sin(angle_current) * outer_radius);
          linalg::vec3f point_next =
              base_center +
              right_axis *
                  (std::cos(angle_next) * outer_radius) +
              up_axis *
                  (std::sin(angle_next) * outer_radius);

          // Base circle
          renderer::draw_line(cmd, point_current, point_next,
                             outer_color);

          // Cone edge lines from apex (4 lines)
          if (s % 8 == 0)
          {
            renderer::draw_line(cmd, p, point_current,
                               outer_color);
          }
        }

        // Inner cone circle (dimmer)
        if (light.spot_inner_degrees > 0.0f &&
            light.spot_inner_degrees < light.spot_outer_degrees)
        {
          color_t inner_color = with_alpha(colors::white, 0x44);
          float clamped_inner =
              std::min(light.spot_inner_degrees, 89.0f);
          float inner_radius =
              light.range *
              std::tan(linalg::to_radians(clamped_inner));

          for (int s = 0; s < segments; s++)
          {
            float angle_current = two_pi * s / segments;
            float angle_next = two_pi * (s + 1) / segments;

            linalg::vec3f point_current =
                base_center +
                right_axis *
                    (std::cos(angle_current) * inner_radius) +
                up_axis *
                    (std::sin(angle_current) * inner_radius);
            linalg::vec3f point_next =
                base_center +
                right_axis *
                    (std::cos(angle_next) * inner_radius) +
                up_axis *
                    (std::sin(angle_next) * inner_radius);

            renderer::draw_line(cmd, point_current, point_next,
                               inner_color);
          }
        }
      }

      // Direction arrow for spot and directional lights
      if (light.light_type != 0)
      {
        float arrow_length =
            (light.light_type == 1) ? light.range * 0.3f : 40.0f;
        renderer::draw_arrow(
            cmd, p, p + normalized_direction * arrow_length,
            light_color);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// ImGui UI
// ---------------------------------------------------------------------------

void Shader_Editor_State::render_ui()
{
  // --- Main shader editor panel ---
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(380, 700), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Shader Editor"))
  {
    // Mesh section
    if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::InputText("Path", mesh_path_buffer, sizeof(mesh_path_buffer));
      ImGui::SameLine();
      if (ImGui::Button("Load"))
      {
        mesh_path = mesh_path_buffer;
        load_preview_mesh();
      }
    }

    // Shader compilation
    if (ImGui::CollapsingHeader("Shader", ImGuiTreeNodeFlags_DefaultOpen))
    {
      auto set_preset = [&](const char *vert, const char *frag)
      {
        vert_shader_path = vert;
        frag_shader_path = frag;
        strncpy(vert_path_buffer, vert, sizeof(vert_path_buffer) - 1);
        strncpy(frag_path_buffer, frag, sizeof(frag_path_buffer) - 1);
        recompile_preview_shaders();
      };

      {
        std::filesystem::path preview_dir(PREVIEW_SHADER_DIR);
        std::filesystem::path shader_dir = preview_dir.parent_path();
        std::string pbr_vert   = (shader_dir / "pbr" / "pbr.vert").string();
        std::string pbr_frag   = (shader_dir / "pbr" / "pbr.frag").string();
        std::string prev_vert  = (preview_dir / "preview.vert").string();
        std::string prev_frag  = (preview_dir / "preview.frag").string();

        if (ImGui::Button("PBR"))   set_preset(pbr_vert.c_str(),  pbr_frag.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Preview")) set_preset(prev_vert.c_str(), prev_frag.c_str());
      }

      ImGui::InputText("Vert", vert_path_buffer, sizeof(vert_path_buffer));
      ImGui::InputText("Frag", frag_path_buffer, sizeof(frag_path_buffer));
      if (ImGui::Button("Recompile"))
      {
        vert_shader_path = vert_path_buffer;
        frag_shader_path = frag_path_buffer;
        recompile_preview_shaders();
      }

      if (!compile_error.empty())
      {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", compile_error.c_str());
        ImGui::PopStyleColor();
      }
      else if (pipeline_ready)
      {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Pipeline OK");
      }
    }

    // PBR material
    if (ImGui::CollapsingHeader("PBR Material", ImGuiTreeNodeFlags_DefaultOpen))
    {
      ImGui::InputText("Folder", pbr_folder_buffer, sizeof(pbr_folder_buffer));
      ImGui::SameLine();
      if (ImGui::Button("Load##mat"))
      {
        pbr_material_folder = pbr_folder_buffer;
        pbr_material_handle = assets::load_pbr_material(pbr_material_folder.c_str());
        if (pbr_material_handle.valid())
        {
          const assets::pbr_material_asset_t *mat = assets::get(pbr_material_handle);
          if (mat)
          {
            vkDeviceWaitIdle(renderer::GetDevice());
            bind_pbr_textures(renderer::GetDevice(), preview_pipeline, *mat);
          }
        }
      }
      if (pbr_material_handle.valid())
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Material loaded");
      else
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No material loaded");
    }

    // Lights section
    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
      if (ImGui::Button("Add Light") &&
          lights.size() < static_cast<size_t>(MAX_LIGHTS))
      {
        editor_light_t new_light;
        new_light.position = camera.position;
        lights.push_back(new_light);
        selected_light_index = static_cast<int>(lights.size()) - 1;
      }

      if (selected_light_index >= 0 &&
          selected_light_index < static_cast<int>(lights.size()))
      {
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
        {
          lights.erase(lights.begin() + selected_light_index);
          if (selected_light_index >= static_cast<int>(lights.size()))
            selected_light_index = static_cast<int>(lights.size()) - 1;
        }
      }

      for (int i = 0; i < static_cast<int>(lights.size()); i++)
      {
        ImGui::PushID(i);
        bool is_selected = (i == selected_light_index);
        char label[32];
        snprintf(label, sizeof(label), "Light %d", i);
        if (ImGui::Selectable(label, is_selected))
        {
          selected_light_index = i;
        }
        ImGui::PopID();
      }

      if (selected_light_index >= 0 &&
          selected_light_index < static_cast<int>(lights.size()))
      {
        auto &light = lights[selected_light_index];
        ImGui::Separator();

        const char *type_names[] = {"Point", "Spot", "Directional"};
        ImGui::Combo("Type", &light.light_type, type_names, 3);
        ImGui::DragFloat3("Position", &light.position.x, 0.1f);
        if (light.light_type != 0)
        {
          ImGui::DragFloat3("Direction", &light.direction.x, 0.01f);
        }
        ImGui::ColorEdit3("Color", &light.color.x);
        ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 100.0f);
        if (light.light_type != 2)
        {
          ImGui::DragFloat("Range", &light.range, 0.5f, 0.1f, 200.0f);
        }
        if (light.light_type == 1)
        {
          ImGui::DragFloat("Inner Angle", &light.spot_inner_degrees, 0.5f, 0.0f,
                           90.0f);
          ImGui::DragFloat("Outer Angle", &light.spot_outer_degrees, 0.5f, 0.0f,
                           90.0f);
        }
      }
    }

    // Parameters section
    if (ImGui::CollapsingHeader("Parameters"))
    {
      ImGui::Text("Colors");
      for (int i = 0; i < PARAM_COLOR_COUNT; i++)
      {
        ImGui::PushID(1000 + i);
        ImGui::InputText("##label", param_color_labels[i], 32);
        ImGui::SameLine();
        ImGui::ColorEdit4(param_color_labels[i], param_colors[i]);
        ImGui::PopID();
      }

      ImGui::Separator();
      ImGui::Text("Floats");
      for (int i = 0; i < PARAM_FLOAT_COUNT; i++)
      {
        ImGui::PushID(2000 + i);
        ImGui::SetNextItemWidth(100);
        ImGui::InputText("##label", param_float_labels[i], 32);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150);
        ImGui::DragFloat("##value", &param_floats[i], 0.01f);
        ImGui::PopID();
      }

      ImGui::Separator();
      ImGui::Text("Vec4s");
      for (int i = 0; i < PARAM_VEC4_COUNT; i++)
      {
        ImGui::PushID(3000 + i);
        ImGui::InputText("##label", param_vec4_labels[i], 32);
        ImGui::SameLine();
        ImGui::DragFloat4("##value", param_vec4s[i], 0.01f);
        ImGui::PopID();
      }
    }

    // Camera info
    if (ImGui::CollapsingHeader("Camera"))
    {
      if (ImGui::DragFloat3("Target", &camera.orbit_target.x, 0.1f))
        update_orbit(camera);
      if (ImGui::DragFloat("Distance", &camera.orbit_distance, 0.1f, 0.5f, 50.0f))
        update_orbit(camera);
      if (ImGui::DragFloat("Yaw", &camera.yaw, 0.5f))
        update_orbit(camera);
      if (ImGui::DragFloat("Pitch", &camera.pitch, 0.5f, -89.0f, 89.0f))
        update_orbit(camera);
    }
  }
  ImGui::End();

  // Position overlay (top right)
  {
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 window_size(220, 0);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x - window_size.x - 10, 10),
        ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);
    if (ImGui::Begin("##position_overlay", nullptr,
                     ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav))
    {
      ImGui::Text("Position: %.1f, %.1f, %.1f",
                  camera.position.x, camera.position.y, camera.position.z);
    }
    ImGui::End();
  }

  // Controls help overlay
  ImGui::SetNextWindowPos(ImVec2(10, 720), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300, 80), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Controls", nullptr,
                   ImGuiWindowFlags_NoFocusOnAppearing))
  {
    ImGui::Text("RMB drag: orbit  |  MMB drag: pan");
    ImGui::Text("Scroll: zoom  |  Escape: main menu");
    ImGui::Text("LMB: select light  |  LMB drag: move along dir");
    ImGui::Checkbox("Invert orbit Y", &invert_orbit_y);
    ImGui::Checkbox("Render Normals", &render_normals);
    ImGui::Checkbox("Render UV", &render_uv);
    ImGui::Checkbox("Render Parallax UV", &render_parallax_uv);
  }
  ImGui::End();
}

} // namespace client
