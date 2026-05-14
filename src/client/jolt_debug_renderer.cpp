#ifdef JPH_DEBUG_RENDERER

#include "jolt_debug_renderer.hpp"
#include "renderer.hpp"
#include "linalg.hpp"

namespace client {

static uint32_t jolt_color_to_abgr(JPH::ColorArg c)
{
    return (uint32_t(c.a) << 24) | (uint32_t(c.b) << 16) | (uint32_t(c.g) << 8) | uint32_t(c.r);
}

jolt_debug_renderer_t::jolt_debug_renderer_t()
{
    Initialize();
}

void jolt_debug_renderer_t::DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color)
{
    if (!command_buffer_)
        return;
    linalg::vec3 a{float(from.GetX()), float(from.GetY()), float(from.GetZ())};
    linalg::vec3 b{float(to.GetX()),   float(to.GetY()),   float(to.GetZ())};
    renderer::DrawLine(command_buffer_, a, b, jolt_color_to_abgr(color));
}

void jolt_debug_renderer_t::DrawText3D(JPH::RVec3Arg, const std::string_view &,
                                        JPH::ColorArg, float)
{
}

} // namespace client

#endif // JPH_DEBUG_RENDERER
