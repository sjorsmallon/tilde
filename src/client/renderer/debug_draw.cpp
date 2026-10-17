#include "renderer/internal.hpp"

// Debug primitives: one shader pair, five pipelines over it, one persistently
// mapped vertex ring per frame in flight. The list it draws is debug_draw_list_t.

const uint32_t debug_vert_spv[] =
#include "debug.vert.spv.h"
    ;

const uint32_t debug_frag_spv[] =
#include "debug.frag.spv.h"
    ;

namespace client
{
namespace renderer
{

static VkPipelineLayout g_debug_pipeline_layout = VK_NULL_HANDLE;
static VkPipeline       g_debug_box_pipeline    = VK_NULL_HANDLE; // triangles, opaque, writes depth
static VkPipeline       g_debug_line_pipeline   = VK_NULL_HANDLE; // lines, dynamic depth bias
static VkPipeline       g_debug_face_pipeline   = VK_NULL_HANDLE; // triangles, alpha, no depth write

// The occluded halves: the same two programs with the depth test INVERTED, so
// they paint exactly the fragments their depth-tested twin rejected. Used for
// the dimmed second draw of an entry flagged debug_line_t::draw_when_occluded.
static VkPipeline g_debug_line_occluded_pipeline = VK_NULL_HANDLE;
static VkPipeline g_debug_face_occluded_pipeline = VK_NULL_HANDLE;



struct debug_vertex_t
{
  float position[3];
  float color[4]; 
  float barycentric[3];
  // 0 opts the rim term out entirely, which is how a line (no surface, so no
  // meaningful normal) and an opaque box share the one program with the overlay
  // faces that do want it. Per VERTEX for the same reason the colour is: one
  // draw covers polygons that disagree about it.
  float rim_strength;
};

// debug.vert's push block: 64 + 16 + 16 = 96 bytes, inside the 128 every
// implementation guarantees.
struct debug_push_constants_t
{
  float mvp[16];
  float color[4];
  float camera_position[4]; // world space; .w unused
};

constexpr uint32_t DEBUG_VERTEX_CAPACITY = 262144;

// How much of an entry's alpha survives into its occluded copy. Low enough that
// the hidden half never competes with the visible one for attention, high enough
// to still read against a lit surface. Applied through the push-constant tint,
// which the vertex shader already multiplies into the vertex alpha -- so the
// occluded pass re-draws the SAME vertices and needs no second copy of
// anything.
constexpr float DEBUG_OCCLUDED_ALPHA_SCALE = 0.3f;

static VkBuffer        g_debug_vertex_buffer[MAX_FRAMES_IN_FLIGHT] = {};
static VkDeviceMemory  g_debug_vertex_memory[MAX_FRAMES_IN_FLIGHT] = {};
static debug_vertex_t *g_debug_vertex_mapped[MAX_FRAMES_IN_FLIGHT] = {};
static uint32_t        g_debug_vertex_used                         = 0;

// Everything a debug pipeline can differ in. Five instances exist; nothing here
// is worth a cache, because unlike materials the set is closed.
struct debug_pipeline_options_t
{
  VkPrimitiveTopology topology  = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  bool blend = false;
  bool depth_write = true;
  bool dynamic_depth_bias = false;
  float line_width = 1.0f;
  VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
  // GREATER is the occluded half: it draws exactly where LESS would have
  // rejected, so a pair of pipelines covers every fragment of the geometry once
  // and neither can double up on the other.
  VkCompareOp depth_compare = VK_COMPARE_OP_LESS;
};

static VkPipeline create_debug_pipeline(const debug_pipeline_options_t &options)
{
  VkShaderModule vert_module = VK_NULL_HANDLE;
  VkShaderModule frag_module = VK_NULL_HANDLE;

  VkShaderModuleCreateInfo vert_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  vert_info.codeSize = sizeof(debug_vert_spv);
  vert_info.pCode    = debug_vert_spv;
  if (vkCreateShaderModule(g_device, &vert_info, nullptr, &vert_module) != VK_SUCCESS)
  {
    log_error("[renderer] failed to create the debug vertex shader module");
    return VK_NULL_HANDLE;
  }

  VkShaderModuleCreateInfo frag_info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  frag_info.codeSize = sizeof(debug_frag_spv);
  frag_info.pCode    = debug_frag_spv;
  if (vkCreateShaderModule(g_device, &frag_info, nullptr, &frag_module) != VK_SUCCESS)
  {
    log_error("[renderer] failed to create the debug fragment shader module");
    vkDestroyShaderModule(g_device, vert_module, nullptr);
    return VK_NULL_HANDLE;
  }

  VkPipelineShaderStageCreateInfo stages[] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
       vert_module, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
       VK_SHADER_STAGE_FRAGMENT_BIT, frag_module, "main", nullptr}};

  VkVertexInputBindingDescription binding{};
  binding.binding   = 0;
  binding.stride    = sizeof(debug_vertex_t);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attributes[4]{};
  attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(debug_vertex_t, position)};
  attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(debug_vertex_t, color)};
  attributes[2] = {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(debug_vertex_t, barycentric)};
  attributes[3] = {3, 0, VK_FORMAT_R32_SFLOAT, offsetof(debug_vertex_t, rim_strength)};

  VkPipelineVertexInputStateCreateInfo vertex_input{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount   = 1;
  vertex_input.pVertexBindingDescriptions      = &binding;
  vertex_input.vertexAttributeDescriptionCount = 4;
  vertex_input.pVertexAttributeDescriptions    = attributes;

  VkPipelineInputAssemblyStateCreateInfo input_assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  input_assembly.topology = options.topology;

  VkPipelineViewportStateCreateInfo viewport_state{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount  = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rasterizer.polygonMode    = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth      = options.line_width;
  rasterizer.cullMode       = options.cull_mode;
  rasterizer.frontFace      = HOUSE_FRONT_FACE;
  rasterizer.depthBiasEnable = options.dynamic_depth_bias ? VK_TRUE : VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth_stencil{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth_stencil.depthTestEnable  = VK_TRUE;
  depth_stencil.depthWriteEnable = options.depth_write ? VK_TRUE : VK_FALSE;
  depth_stencil.depthCompareOp   = options.depth_compare;

  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  blend_attachment.blendEnable         = options.blend ? VK_TRUE : VK_FALSE;
  blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
  const scene_blend_attachments_t blend_attachments =
      scene_blend_attachments(blend_attachment, false);

  VkPipelineColorBlendStateCreateInfo color_blending{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  color_blending.attachmentCount = blend_attachments.size();
  color_blending.pAttachments    = blend_attachments.data;

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                     VK_DYNAMIC_STATE_DEPTH_BIAS};
  VkPipelineDynamicStateCreateInfo dynamic_state{
      VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic_state.dynamicStateCount = options.dynamic_depth_bias ? 3 : 2;
  dynamic_state.pDynamicStates    = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  pipeline_info.stageCount          = 2;
  pipeline_info.pStages             = stages;
  pipeline_info.pVertexInputState   = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState      = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState   = &multisampling;
  pipeline_info.pDepthStencilState  = &depth_stencil;
  pipeline_info.pColorBlendState    = &color_blending;
  pipeline_info.pDynamicState       = &dynamic_state;
  pipeline_info.layout              = g_debug_pipeline_layout;
  pipeline_info.renderPass          = g_scene_render_pass;
  pipeline_info.subpass             = 0;

  VkPipeline pipeline = VK_NULL_HANDLE;
  if (vkCreateGraphicsPipelines(g_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) !=
      VK_SUCCESS)
  {
    log_error("[renderer] failed to create a debug pipeline");
  }

  vkDestroyShaderModule(g_device, frag_module, nullptr);
  vkDestroyShaderModule(g_device, vert_module, nullptr);
  return pipeline;
}

void create_debug_resources()
{
  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_range.offset     = 0;
  push_range.size       = sizeof(debug_push_constants_t);

  VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_info.setLayoutCount         = 0;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges    = &push_range;

  if (vkCreatePipelineLayout(g_device, &layout_info, nullptr, &g_debug_pipeline_layout) !=
      VK_SUCCESS)
  {
    fatal_error("[renderer] could not create the debug pipeline layout");
  }

  // A solid box writes depth and is edge-darkened; a line takes a per-run depth
  // bias so an outline can be pulled in front of the surface it traces; a filled
  // polygon is translucent and must not write depth or it would hide what is
  // behind it.
  g_debug_box_pipeline = create_debug_pipeline({.blend = true, .dynamic_depth_bias = true});
  g_debug_line_pipeline =
      create_debug_pipeline({.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                             .blend              = true,
                             .dynamic_depth_bias = true,
                             .line_width         = 2.0f,
                             .cull_mode          = VK_CULL_MODE_NONE});
  // A face highlight is coplanar with what it highlights, so it needs the same
  // per-run pull toward the camera a traced outline does.
  g_debug_face_pipeline = create_debug_pipeline({.blend              = true,
                                                 .depth_write        = false,
                                                 .dynamic_depth_bias = true,
                                                 .cull_mode          = VK_CULL_MODE_NONE});

  // The occluded halves. Neither writes depth -- one of these draws is a hint
  // about something that is BEHIND the scene, and letting it write would sort
  // real geometry against a hint.
  g_debug_line_occluded_pipeline =
      create_debug_pipeline({.topology           = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
                             .blend              = true,
                             .depth_write        = false,
                             .dynamic_depth_bias = true,
                             .line_width         = 2.0f,
                             .cull_mode          = VK_CULL_MODE_NONE,
                             .depth_compare      = VK_COMPARE_OP_GREATER});
  g_debug_face_occluded_pipeline =
      create_debug_pipeline({.blend              = true,
                             .depth_write        = false,
                             .dynamic_depth_bias = true,
                             .cull_mode          = VK_CULL_MODE_NONE,
                             .depth_compare      = VK_COMPARE_OP_GREATER});

  const VkDeviceSize buffer_size = sizeof(debug_vertex_t) * DEBUG_VERTEX_CAPACITY;
  for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
  {
    create_buffer(buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                  g_debug_vertex_buffer[frame], g_debug_vertex_memory[frame]);
    vkMapMemory(g_device, g_debug_vertex_memory[frame], 0, buffer_size, 0,
                (void **)&g_debug_vertex_mapped[frame]);
  }
}

void destroy_debug_resources()
{
  for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
  {
    if (g_debug_vertex_memory[frame])
    {
      vkUnmapMemory(g_device, g_debug_vertex_memory[frame]);
      vkFreeMemory(g_device, g_debug_vertex_memory[frame], nullptr);
    }
    if (g_debug_vertex_buffer[frame])
      vkDestroyBuffer(g_device, g_debug_vertex_buffer[frame], nullptr);
  }

  for (VkPipeline pipeline : {g_debug_box_pipeline, g_debug_line_pipeline, g_debug_face_pipeline,
                              g_debug_line_occluded_pipeline, g_debug_face_occluded_pipeline})
    if (pipeline)
      vkDestroyPipeline(g_device, pipeline, nullptr);

  if (g_debug_pipeline_layout)
    vkDestroyPipelineLayout(g_device, g_debug_pipeline_layout, nullptr);
}

// Reserve `count` vertices out of this frame's debug buffer. Null means the
// frame overflowed -- it LOGS rather than returning quietly, because a debug
// overlay that silently stops drawing is worse than one that stutters.
static debug_vertex_t *allocate_debug_vertices(uint32_t count, uint32_t &out_first_vertex)
{
  if (g_debug_vertex_used + count > DEBUG_VERTEX_CAPACITY)
  {
    log_error("[renderer] debug vertex buffer full ({} of {} used, wanted {} more)",
              g_debug_vertex_used, DEBUG_VERTEX_CAPACITY, count);
    return nullptr;
  }

  out_first_vertex = g_debug_vertex_used;
  debug_vertex_t *storage = g_debug_vertex_mapped[g_current_frame_idx_in_swapchain] +
                            g_debug_vertex_used;
  g_debug_vertex_used += count;
  return storage;
}

void reset_debug_vertices()
{
  g_debug_vertex_used = 0;
}

// --- Debug primitive recording ---

static void write_debug_vertex(debug_vertex_t &vertex, const linalg::vec3f& position, color_t color,
                               float barycentric_x, float barycentric_y, float barycentric_z,
                               float rim_strength = 0.0f)
{
  vertex.position[0]    = position.x;
  vertex.position[1]    = position.y;
  vertex.position[2]    = position.z;
  vertex.color[0]       = color.r / 255.0f;
  vertex.color[1]       = color.g / 255.0f;
  vertex.color[2]       = color.b / 255.0f;
  vertex.color[3]       = color.a / 255.0f;
  vertex.barycentric[0] = barycentric_x;
  vertex.barycentric[1] = barycentric_y;
  vertex.barycentric[2] = barycentric_z;
  vertex.rim_strength   = rim_strength;
}

static void push_debug_constants(VkCommandBuffer cmd, const linalg::mat4f &mvp, color_t tint,
                          const linalg::vec3f& camera_position)
{
  debug_push_constants_t push{};
  memcpy(push.mvp, &mvp, sizeof(push.mvp));
  push.color[0] = tint.r / 255.0f;
  push.color[1] = tint.g / 255.0f;
  push.color[2] = tint.b / 255.0f;
  push.color[3] = tint.a / 255.0f;
  // The rim term needs a per-pixel vector to the eye, and the vertex stage is
  // where it is cheapest to start one. Nothing else in the debug path reads it.
  push.camera_position[0] = camera_position.x;
  push.camera_position[1] = camera_position.y;
  push.camera_position[2] = camera_position.z;
  vkCmdPushConstants(cmd, g_debug_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push),
                     &push);
}

// The barycentric a polygon's Nth CORNER carries when it wants edge darkening.
// Straight off the unit cube the solid box used to be drawn with: a quad's four
// corners get red, green, blue, green, so the fan's shared diagonal is not
// darkened but the four outer edges are.
static void shaded_corner_barycentric(uint32_t corner, float &out_x, float &out_y, float &out_z)
{
  static constexpr float TABLE[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 1, 0}};
  const float *entry = TABLE[corner < 4 ? corner : (corner % 3)];
  out_x              = entry[0];
  out_y              = entry[1];
  out_z              = entry[2];
}

void record_debug_lines(VkCommandBuffer cmd, const debug_draw_list_t &debug,
                        const linalg::mat4f &view_projection_matrix,
                        const linalg::vec3f& camera_position)
{
  if (debug.lines.empty() || g_debug_line_pipeline == VK_NULL_HANDLE)
    return;

  uint32_t        first_vertex = 0;
  debug_vertex_t *storage = allocate_debug_vertices((uint32_t)debug.lines.size() * 2, first_vertex);
  if (!storage)
    return;

  for (size_t index = 0; index < debug.lines.size(); ++index)
  {
    const debug_line_t &line = debug.lines[index];
    write_debug_vertex(storage[index * 2 + 0], line.start, line.color, 1, 1, 1);
    write_debug_vertex(storage[index * 2 + 1], line.end, line.color, 1, 1, 1);
  }

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_debug_line_pipeline);
  const VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &g_debug_vertex_buffer[g_current_frame_idx_in_swapchain],
                         &offset);
  push_debug_constants(cmd, view_projection_matrix, colors::white, camera_position);

  // Depth bias is pipeline DYNAMIC state, so one draw can only carry one value.
  // Lines are emitted in runs of equal bias -- which is what makes a -200
  // selection outline actually take effect, where the old single flat batch
  // silently drew the whole frame with whichever bias happened to be set last.
  size_t run_start = 0;
  while (run_start < debug.lines.size())
  {
    const float bias    = debug.lines[run_start].depth_bias;
    size_t      run_end = run_start + 1;
    while (run_end < debug.lines.size() && debug.lines[run_end].depth_bias == bias)
      ++run_end;

    // A bias of 0 still gets the pipeline's baseline pull, which is what keeps an
    // ordinary debug line from z-fighting the surface it sits on.
    vkCmdSetDepthBias(cmd, -2.0f + bias, 0.0f, -1.0f + bias * 0.05f);
    vkCmdDraw(cmd, (uint32_t)(run_end - run_start) * 2,
              1, first_vertex + (uint32_t)run_start * 2, 0);
    run_start = run_end;
  }

  // The hidden halves, dimmed, over runs of consecutive flagged lines. The
  // vertices are the ones already written above -- the second copy costs draws,
  // not geometry -- which is also why the runs are found rather than sorted: a
  // reorder would invalidate the offsets the pass above just drew from.
  if (g_debug_line_occluded_pipeline == VK_NULL_HANDLE)
    return;

  bool occluded_pipeline_bound = false;
  size_t occluded_start = 0;
  while (occluded_start < debug.lines.size())
  {
    if (!debug.lines[occluded_start].draw_when_occluded)
    {
      ++occluded_start;
      continue;
    }

    size_t occluded_end = occluded_start + 1;
    while (occluded_end < debug.lines.size() && debug.lines[occluded_end].draw_when_occluded)
      ++occluded_end;

    if (!occluded_pipeline_bound)
    {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_debug_line_occluded_pipeline);
      push_debug_constants(
          cmd, view_projection_matrix,
          with_alpha(colors::white, color_channel_from_float(DEBUG_OCCLUDED_ALPHA_SCALE)),
          camera_position);
      // One bias for the whole pass: the per-line values exist to stop an
      // outline z-fighting the surface it traces, and nothing drawn here is
      // coplanar with anything by construction -- it is all strictly behind.
      vkCmdSetDepthBias(cmd, -2.0f, 0.0f, -1.0f);
      occluded_pipeline_bound = true;
    }

    vkCmdDraw(cmd, (uint32_t)(occluded_end - occluded_start) * 2, 1,
              first_vertex + (uint32_t)occluded_start * 2, 0);
    occluded_start = occluded_end;
  }
}

// Polygons split by ALPHA, not by who appended them: a fully opaque face writes
// depth so later geometry sorts against it, and a translucent one must not or it
// would hide what is behind it. That is the whole difference between the two
// pipelines, and reading it off the colour means a caller never picks.
void record_debug_polygons(VkCommandBuffer cmd, const debug_draw_list_t &debug,
                           const linalg::mat4f &view_projection_matrix,
                           const linalg::vec3f& camera_position, bool opaque_pass)
{
  const VkPipeline pipeline = opaque_pass ? g_debug_box_pipeline : g_debug_face_pipeline;
  if (debug.polygons.empty() || pipeline == VK_NULL_HANDLE)
    return;

  const auto belongs = [opaque_pass](const debug_polygon_t &polygon) {
    return (polygon.color.a == 255) == opaque_pass;
  };

  // Triangle-fan decomposed into a triangle list, so every polygon shares one
  // pipeline and one bind regardless of vertex count.
  uint32_t triangle_vertex_count = 0;
  for (const debug_polygon_t &polygon : debug.polygons)
    if (polygon.vertex_count >= 3 && belongs(polygon))
      triangle_vertex_count += (polygon.vertex_count - 2) * 3;

  if (triangle_vertex_count == 0)
    return;

  uint32_t        first_vertex = 0;
  debug_vertex_t *storage      = allocate_debug_vertices(triangle_vertex_count, first_vertex);
  if (!storage)
    return;

  // Written in two groups -- everything unflagged, then everything flagged -- so
  // the occluded copy is one contiguous sub-range and one extra draw. Grouping
  // at WRITE time is what keeps it to one; the alternative is a draw per flagged
  // polygon.
  uint32_t written = 0;

  const auto write_polygon = [&](const debug_polygon_t &polygon) {
    // (1,1,1) saturates the shader's smoothstep, so an unshaded polygon opts out
    // of the edge darkening without the shader needing a mode.
    const auto corner_barycentric = [&polygon](uint32_t corner, float &x, float &y, float &z) {
      if (!polygon.style.shaded)
      {
        x = y = z = 1.0f;
        return;
      }
      shaded_corner_barycentric(corner, x, y, z);
    };

    const float rim = polygon.style.rim ? 1.0f : 0.0f;

    const linalg::vec3f *vertices = &debug.polygon_vertices[polygon.first_vertex];
    for (uint32_t corner = 1; corner + 1 < polygon.vertex_count; ++corner)
    {
      float x = 0, y = 0, z = 0;
      corner_barycentric(0, x, y, z);
      write_debug_vertex(storage[written++], vertices[0], polygon.color, x, y, z, rim);
      corner_barycentric(corner, x, y, z);
      write_debug_vertex(storage[written++], vertices[corner], polygon.color, x, y, z, rim);
      corner_barycentric(corner + 1, x, y, z);
      write_debug_vertex(storage[written++], vertices[corner + 1], polygon.color, x, y, z, rim);
    }
  };

  // Runs of equal bias, for the reason record_debug_lines already spells out:
  // depth bias is dynamic state, so one draw carries one value.
  struct bias_run_t
  {
    float    bias;
    uint32_t first;
    uint32_t count;
  };
  std::vector<bias_run_t> runs;

  const auto write_polygon_into_runs = [&](const debug_polygon_t &polygon) {
    const uint32_t before = written;
    write_polygon(polygon);
    if (written == before)
      return;

    if (!runs.empty() && runs.back().bias == polygon.style.depth_bias)
      runs.back().count += written - before;
    else
      runs.push_back({polygon.style.depth_bias, before, written - before});
  };

  for (const debug_polygon_t &polygon : debug.polygons)
    if (polygon.vertex_count >= 3 && belongs(polygon) && !polygon.style.draw_when_occluded)
      write_polygon_into_runs(polygon);

  const uint32_t first_occluded_vertex = written;
  const size_t   visible_run_count     = runs.size();

  for (const debug_polygon_t &polygon : debug.polygons)
    if (polygon.vertex_count >= 3 && belongs(polygon) && polygon.style.draw_when_occluded)
      write_polygon(polygon);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  const VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &g_debug_vertex_buffer[g_current_frame_idx_in_swapchain],
                         &offset);
  push_debug_constants(cmd, view_projection_matrix, colors::white, camera_position);

  for (size_t index = 0; index < visible_run_count; ++index)
  {
    const bias_run_t &run = runs[index];
    vkCmdSetDepthBias(cmd, -2.0f + run.bias, 0.0f, -1.0f + run.bias * 0.05f);
    vkCmdDraw(cmd, run.count, 1, first_vertex + run.first, 0);
  }

  if (written == first_occluded_vertex || g_debug_face_occluded_pipeline == VK_NULL_HANDLE)
    return;

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_debug_face_occluded_pipeline);
  push_debug_constants(
      cmd, view_projection_matrix,
      with_alpha(colors::white, color_channel_from_float(DEBUG_OCCLUDED_ALPHA_SCALE)),
      camera_position);
  // One bias for the pass: everything here is strictly behind the scene.
  vkCmdSetDepthBias(cmd, -2.0f, 0.0f, -1.0f);
  vkCmdDraw(cmd, written - first_occluded_vertex, 1, first_vertex + first_occluded_vertex, 0);
}

} // namespace renderer
} // namespace client
