#include "renderer.hpp"

#include <iostream>
#include <optional>
#include <set>
#include <vector>

#include "SDL_vulkan.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include "log.hpp" // Utilizing existing log system

namespace client {
namespace renderer {

// --- Globals (Internal) ---
static SDL_Window *g_window = nullptr;
static VkInstance g_instance = VK_NULL_HANDLE;
static VkPhysicalDevice g_physical_device = VK_NULL_HANDLE;
static VkDevice g_device = VK_NULL_HANDLE;
static VkQueue g_graphics_queue = VK_NULL_HANDLE;
static VkQueue g_present_queue = VK_NULL_HANDLE;
static VkSurfaceKHR g_surface = VK_NULL_HANDLE;
static VkSwapchainKHR g_swapchain = VK_NULL_HANDLE;
static std::vector<VkImage> g_swapchain_images;
static std::vector<VkImageView> g_swapchain_image_views;
static VkFormat g_swapchain_image_format;
static VkExtent2D g_swapchain_extent;

static VkRenderPass g_render_pass = VK_NULL_HANDLE;
static VkDescriptorPool g_descriptor_pool = VK_NULL_HANDLE;
static VkCommandPool g_command_pool = VK_NULL_HANDLE;
static std::vector<VkFramebuffer> g_swapchain_framebuffers;
static std::vector<VkCommandBuffer> g_command_buffers;

static std::vector<VkSemaphore> g_image_available_semaphores;
static std::vector<VkSemaphore> g_render_finished_semaphores;
static std::vector<VkFence> g_in_flight_fences;

static uint32_t g_current_frame = 0;
const int MAX_FRAMES_IN_FLIGHT = 2;
static bool g_swapchain_rebuild = false;
static uint32_t g_image_index = 0; // Stored between BeginFrame and EndFrame

// --- Helper Functions ---

static void check_vk_result(VkResult err) {
  if (err == 0)
    return;
  log_error("[vulkan] Error: VkResult = {}", (int)err);
  if (err < 0)
    abort();
}

struct QueueFamilyIndices {
  std::optional<uint32_t> graphics_family;
  std::optional<uint32_t> present_family;

  bool is_complete() {
    return graphics_family.has_value() && present_family.has_value();
  }
};

static QueueFamilyIndices find_queue_families(VkPhysicalDevice device) {
  QueueFamilyIndices indices;
  uint32_t queue_family_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count,
                                           queue_families.data());

  int i = 0;
  for (const auto &queue_family : queue_families) {
    if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphics_family = i;
    }
    VkBool32 present_support = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, g_surface,
                                         &present_support);
    if (present_support) {
      indices.present_family = i;
    }
    if (indices.is_complete())
      break;
    i++;
  }
  return indices;
}

static void cleanup_swapchain() {
  for (auto framebuffer : g_swapchain_framebuffers) {
    vkDestroyFramebuffer(g_device, framebuffer, nullptr);
  }
  for (auto imageView : g_swapchain_image_views) {
    vkDestroyImageView(g_device, imageView, nullptr);
  }
  vkDestroySwapchainKHR(g_device, g_swapchain, nullptr);
}

static void create_swapchain() {
  VkSurfaceCapabilitiesKHR capabilities;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_physical_device, g_surface,
                                            &capabilities);

  uint32_t format_count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device, g_surface,
                                       &format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(g_physical_device, g_surface,
                                       &format_count, formats.data());

  uint32_t present_mode_count;
  vkGetPhysicalDeviceSurfacePresentModesKHR(g_physical_device, g_surface,
                                            &present_mode_count, nullptr);
  std::vector<VkPresentModeKHR> present_modes(present_mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      g_physical_device, g_surface, &present_mode_count, present_modes.data());

  VkSurfaceFormatKHR surface_format = formats[0];
  for (const auto &available_format : formats) {
    if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      surface_format = available_format;
      break;
    }
  }

  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  VkExtent2D extent;
  if (capabilities.currentExtent.width != UINT32_MAX) {
    extent = capabilities.currentExtent;
  } else {
    int width, height;
    SDL_Vulkan_GetDrawableSize(g_window, &width, &height);
    extent = {(uint32_t)width, (uint32_t)height};
  }

  uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      image_count > capabilities.maxImageCount) {
    image_count = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = g_surface;
  create_info.minImageCount = image_count;
  create_info.imageFormat = surface_format.format;
  create_info.imageColorSpace = surface_format.colorSpace;
  create_info.imageExtent = extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  QueueFamilyIndices indices = find_queue_families(g_physical_device);
  uint32_t queue_family_indices[] = {indices.graphics_family.value(),
                                     indices.present_family.value()};

  if (indices.graphics_family != indices.present_family) {
    create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    create_info.queueFamilyIndexCount = 2;
    create_info.pQueueFamilyIndices = queue_family_indices;
  } else {
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  create_info.preTransform = capabilities.currentTransform;
  create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  create_info.presentMode = present_mode;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = VK_NULL_HANDLE;

  if (vkCreateSwapchainKHR(g_device, &create_info, nullptr, &g_swapchain) !=
      VK_SUCCESS) {
    log_error("Failed to create swapchain!");
    return;
  }

  vkGetSwapchainImagesKHR(g_device, g_swapchain, &image_count, nullptr);
  g_swapchain_images.resize(image_count);
  vkGetSwapchainImagesKHR(g_device, g_swapchain, &image_count,
                          g_swapchain_images.data());

  g_swapchain_image_format = surface_format.format;
  g_swapchain_extent = extent;

  g_swapchain_image_views.resize(g_swapchain_images.size());
  for (size_t i = 0; i < g_swapchain_images.size(); i++) {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = g_swapchain_images[i];
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = g_swapchain_image_format;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;

    if (vkCreateImageView(g_device, &create_info, nullptr,
                          &g_swapchain_image_views[i]) != VK_SUCCESS) {
      log_error("Failed to create image views!");
    }
  }
}

static void create_framebuffers() {
  g_swapchain_framebuffers.resize(g_swapchain_image_views.size());
  for (size_t i = 0; i < g_swapchain_image_views.size(); i++) {
    VkImageView attachments[] = {g_swapchain_image_views[i]};

    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = g_render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = attachments;
    framebuffer_info.width = g_swapchain_extent.width;
    framebuffer_info.height = g_swapchain_extent.height;
    framebuffer_info.layers = 1;

    if (vkCreateFramebuffer(g_device, &framebuffer_info, nullptr,
                            &g_swapchain_framebuffers[i]) != VK_SUCCESS) {
      log_error("Failed to create framebuffer!");
    }
  }
}

static void rebuild_swapchain() {
  int width = 0, height = 0;
  SDL_Vulkan_GetDrawableSize(g_window, &width, &height);
  if (width == 0 || height == 0)
    return;

  vkDeviceWaitIdle(g_device);
  cleanup_swapchain();
  create_swapchain();
  create_framebuffers();
}

// --- Public API ---

void ProcessEvent(const SDL_Event *event) {
  ImGui_ImplSDL2_ProcessEvent(event);
  if (event->type == SDL_WINDOWEVENT &&
      event->window.event == SDL_WINDOWEVENT_RESIZED &&
      event->window.windowID == SDL_GetWindowID(g_window)) {
    g_swapchain_rebuild = true;
  }
}

bool Init(SDL_Window *window) {
  g_window = window;

  // Vulkan Init
  unsigned int count;
  SDL_Vulkan_GetInstanceExtensions(g_window, &count, nullptr);
  std::vector<const char *> extensions(count);
  SDL_Vulkan_GetInstanceExtensions(g_window, &count, extensions.data());

  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "MyGame";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "No Engine";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo instance_info = {};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &app_info;

#ifdef __APPLE__
  std::vector<const char *> appleExtensions = extensions;
  appleExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
  instance_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  instance_info.enabledExtensionCount = (uint32_t)appleExtensions.size();
  instance_info.ppEnabledExtensionNames = appleExtensions.data();
#else
  instance_info.enabledExtensionCount = (uint32_t)extensions.size();
  instance_info.ppEnabledExtensionNames = extensions.data();
#endif

  if (vkCreateInstance(&instance_info, nullptr, &g_instance) != VK_SUCCESS) {
    log_error("Failed to create Vulkan instance!");
    return false;
  }
  log_terminal("Vulkan Instance created.");

  if (!SDL_Vulkan_CreateSurface(g_window, g_instance, &g_surface)) {
    log_error("Failed to create Vulkan surface: {}", SDL_GetError());
    return false;
  }

  // Physical Device
  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(g_instance, &device_count, nullptr);
  if (device_count == 0) {
    log_error("Failed to find GPUs with Vulkan support!");
    return false;
  }
  std::vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(g_instance, &device_count, devices.data());
  g_physical_device = devices[0];

  // Logical Device
  QueueFamilyIndices indices = find_queue_families(g_physical_device);
  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  std::set<uint32_t> unique_queue_families = {indices.graphics_family.value(),
                                              indices.present_family.value()};

  float queue_priority = 1.0f;
  for (uint32_t queue_family : unique_queue_families) {
    VkDeviceQueueCreateInfo queue_create_info{};
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.queueFamilyIndex = queue_family;
    queue_create_info.queueCount = 1;
    queue_create_info.pQueuePriorities = &queue_priority;
    queue_create_infos.push_back(queue_create_info);
  }

  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount =
      static_cast<uint32_t>(queue_create_infos.size());
  device_info.pQueueCreateInfos = queue_create_infos.data();
  device_info.pEnabledFeatures = nullptr;

  const std::vector<const char *> device_extensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  device_info.enabledExtensionCount =
      static_cast<uint32_t>(device_extensions.size());
  device_info.ppEnabledExtensionNames = device_extensions.data();

#ifdef __APPLE__
  std::vector<const char *> apple_device_extensions = device_extensions;
  apple_device_extensions.push_back("VK_KHR_portability_subset");
  device_info.enabledExtensionCount = (uint32_t)apple_device_extensions.size();
  device_info.ppEnabledExtensionNames = apple_device_extensions.data();
#endif

  if (vkCreateDevice(g_physical_device, &device_info, nullptr, &g_device) !=
      VK_SUCCESS) {
    log_error("Failed to create logical device!");
    return false;
  }

  vkGetDeviceQueue(g_device, indices.graphics_family.value(), 0,
                   &g_graphics_queue);
  vkGetDeviceQueue(g_device, indices.present_family.value(), 0,
                   &g_present_queue);

  // Descriptor Pool
  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
  };
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
  pool_info.pPoolSizes = pool_sizes;
  vkCreateDescriptorPool(g_device, &pool_info, nullptr, &g_descriptor_pool);

  create_swapchain();

  // Render Pass
  VkAttachmentDescription color_attachment{};
  color_attachment.format = g_swapchain_image_format;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_ref{};
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo render_pass_info{};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &color_attachment;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 1;
  render_pass_info.pDependencies = &dependency;

  if (vkCreateRenderPass(g_device, &render_pass_info, nullptr,
                         &g_render_pass) != VK_SUCCESS) {
    log_error("Failed to create render pass!");
    return false;
  }

  create_framebuffers();

  // Command Pool
  VkCommandPoolCreateInfo command_pool_info{};
  command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  command_pool_info.queueFamilyIndex = indices.graphics_family.value();
  if (vkCreateCommandPool(g_device, &command_pool_info, nullptr,
                          &g_command_pool) != VK_SUCCESS) {
    log_error("Failed to create command pool!");
    return false;
  }

  g_command_buffers.resize(MAX_FRAMES_IN_FLIGHT);
  VkCommandBufferAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_info.commandPool = g_command_pool;
  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_info.commandBufferCount = (uint32_t)g_command_buffers.size();

  if (vkAllocateCommandBuffers(g_device, &alloc_info,
                               g_command_buffers.data()) != VK_SUCCESS) {
    log_error("Failed to allocate command buffers!");
    return false;
  }

  // Sync Objects
  g_image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
  g_render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
  g_in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (vkCreateSemaphore(g_device, &semaphore_info, nullptr,
                          &g_image_available_semaphores[i]) != VK_SUCCESS ||
        vkCreateSemaphore(g_device, &semaphore_info, nullptr,
                          &g_render_finished_semaphores[i]) != VK_SUCCESS ||
        vkCreateFence(g_device, &fence_info, nullptr, &g_in_flight_fences[i]) !=
            VK_SUCCESS) {
      log_error("Failed to create sync objects!");
      return false;
    }
  }

  // ImGui
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark();

  ImGui_ImplSDL2_InitForVulkan(g_window);
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = g_instance;
  init_info.PhysicalDevice = g_physical_device;
  init_info.Device = g_device;
  init_info.QueueFamily = indices.graphics_family.value();
  init_info.Queue = g_graphics_queue;
  init_info.PipelineCache = VK_NULL_HANDLE;
  init_info.DescriptorPool = g_descriptor_pool;
  init_info.Subpass = 0;
  init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
  init_info.ImageCount = MAX_FRAMES_IN_FLIGHT;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.Allocator = nullptr;
  init_info.CheckVkResultFn = check_vk_result;
  ImGui_ImplVulkan_Init(&init_info, g_render_pass);

  // Fonts
  {
    VkCommandBuffer command_buffer = g_command_buffers[0];
    vkResetCommandPool(g_device, g_command_pool, 0);
    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(command_buffer, &begin_info);

    ImGui_ImplVulkan_CreateFontsTexture();

    vkEndCommandBuffer(command_buffer);
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_buffer;
    vkQueueSubmit(g_graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkDeviceWaitIdle(g_device);
  }

  return true;
}

void Shutdown() {
  vkDeviceWaitIdle(g_device);

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  cleanup_swapchain();
  vkDestroyDescriptorPool(g_device, g_descriptor_pool, nullptr);

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(g_device, g_render_finished_semaphores[i], nullptr);
    vkDestroySemaphore(g_device, g_image_available_semaphores[i], nullptr);
    vkDestroyFence(g_device, g_in_flight_fences[i], nullptr);
  }

  vkDestroyCommandPool(g_device, g_command_pool, nullptr);
  vkDestroyRenderPass(g_device, g_render_pass, nullptr);
  vkDestroyDevice(g_device, nullptr);

  if (g_surface) {
    vkDestroySurfaceKHR(g_instance, g_surface, nullptr);
  }
  if (g_instance) {
    vkDestroyInstance(g_instance, nullptr);
  }
  // Window is destroyed by client_impl (or passed in) but client_impl owns it?
  // Our Init took SDL_Window*, so we don't own it.
}

VkCommandBuffer BeginFrame() {
  if (g_swapchain_rebuild) {
    rebuild_swapchain();
    g_swapchain_rebuild = false;
  }

  vkWaitForFences(g_device, 1, &g_in_flight_fences[g_current_frame], VK_TRUE,
                  UINT64_MAX);

  VkResult result =
      vkAcquireNextImageKHR(g_device, g_swapchain, UINT64_MAX,
                            g_image_available_semaphores[g_current_frame],
                            VK_NULL_HANDLE, &g_image_index);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    g_swapchain_rebuild = true;
    return VK_NULL_HANDLE;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    log_error("Failed to acquire swap chain image!");
    return VK_NULL_HANDLE;
  }

  vkResetFences(g_device, 1, &g_in_flight_fences[g_current_frame]);

  vkResetCommandBuffer(g_command_buffers[g_current_frame], 0);
  VkCommandBuffer cmdbuf = g_command_buffers[g_current_frame];

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  if (vkBeginCommandBuffer(cmdbuf, &begin_info) != VK_SUCCESS) {
    log_error("Failed to begin command buffer!");
    return VK_NULL_HANDLE;
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  return cmdbuf;
}

void BeginRenderPass(VkCommandBuffer cmd) {
  VkRenderPassBeginInfo render_pass_info{};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_info.renderPass = g_render_pass;
  render_pass_info.framebuffer = g_swapchain_framebuffers[g_image_index];
  render_pass_info.renderArea.offset = {0, 0};
  render_pass_info.renderArea.extent = g_swapchain_extent;

  VkClearValue clear_color = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
  render_pass_info.clearValueCount = 1;
  render_pass_info.pClearValues = &clear_color;

  vkCmdBeginRenderPass(cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
}

void EndFrame(VkCommandBuffer cmd) {
  ImGui::Render();
  ImDrawData *draw_data = ImGui::GetDrawData();

  ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
  vkCmdEndRenderPass(cmd);

  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
    log_error("Failed to record command buffer!");
    return;
  }

  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore wait_semaphores[] = {
      g_image_available_semaphores[g_current_frame]};
  VkPipelineStageFlags wait_stages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = wait_semaphores;
  submit_info.pWaitDstStageMask = wait_stages;

  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &cmd;

  VkSemaphore signal_semaphores[] = {
      g_render_finished_semaphores[g_current_frame]};
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = signal_semaphores;

  if (vkQueueSubmit(g_graphics_queue, 1, &submit_info,
                    g_in_flight_fences[g_current_frame]) != VK_SUCCESS) {
    log_error("Failed to submit draw command buffer!");
    return;
  }

  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_semaphores;

  VkSwapchainKHR swapchains[] = {g_swapchain};
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swapchains;
  present_info.pImageIndices = &g_image_index;

  VkResult result = vkQueuePresentKHR(g_present_queue, &present_info);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    g_swapchain_rebuild = true;
  } else if (result != VK_SUCCESS) {
    log_error("Failed to present swap chain image!");
  }

  g_current_frame = (g_current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

VkDevice GetDevice() { return g_device; }

} // namespace renderer
} // namespace client
