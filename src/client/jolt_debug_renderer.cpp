#ifdef JPH_DEBUG_RENDERER

#include "jolt_debug_renderer.hpp"
#include "renderer.hpp"
#include "linalg.hpp"

namespace client {

static color_t jolt_color_to_color(JPH::ColorArg c)
{
    return color_t{c.r, c.g, c.b, c.a};
}

jolt_debug_renderer_t::jolt_debug_renderer_t()
{
    Initialize();
}

void jolt_debug_renderer_t::DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color)
{
    if (!debug_)
        return;
    linalg::vec3f a{float(from.GetX()), float(from.GetY()), float(from.GetZ())};
    linalg::vec3f b{float(to.GetX()),   float(to.GetY()),   float(to.GetZ())};
    debug_->line(a, b, jolt_color_to_color(color));
}

void jolt_debug_renderer_t::DrawText3D(JPH::RVec3Arg, const std::string_view &,
                                        JPH::ColorArg, float)
{
}

} // namespace client

#endif // JPH_DEBUG_RENDERER
