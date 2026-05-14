#pragma once

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRendererSimple.h>
#include <vulkan/vulkan.h>

namespace client {

class jolt_debug_renderer_t final : public JPH::DebugRendererSimple
{
public:
    jolt_debug_renderer_t();

    void set_command_buffer(VkCommandBuffer cmd) { command_buffer_ = cmd; }

    void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override;
    void DrawText3D(JPH::RVec3Arg position, const std::string_view &str,
                    JPH::ColorArg color, float height) override;

private:
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
};

} // namespace client

#endif // JPH_DEBUG_RENDERER
