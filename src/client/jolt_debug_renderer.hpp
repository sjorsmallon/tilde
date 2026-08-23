#pragma once

#ifdef JPH_DEBUG_RENDERER

#include <Jolt/Jolt.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

#include "renderer.hpp"

namespace client {

class jolt_debug_renderer_t final : public JPH::DebugRendererSimple
{
public:
    jolt_debug_renderer_t();

    // Where the lines go. Null means "not drawing this frame" -- Jolt calls
    // DrawLine from inside its own traversal, so there is no return value to
    // check and the bucket has to be set before and cleared after.
    void set_debug_list(renderer::debug_draw_list_t *debug) { debug_ = debug; }

    void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override;
    void DrawText3D(JPH::RVec3Arg position, const std::string_view &str,
                    JPH::ColorArg color, float height) override;

private:
    renderer::debug_draw_list_t *debug_ = nullptr;
};

} // namespace client

#endif // JPH_DEBUG_RENDERER
