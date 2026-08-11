#include "../../shared/cosmetic_events.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/physics.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"
#include "../renderer.hpp"

namespace client::effects
{

void on_bullet_impact(client_context_t &context,
                      const shared::effect_data_t &data)
{
    (void)context;
    (void)data;
    // draw decal on the impact location.
    linalg::vec3f impact_position = data.origin;
    // get surface normal from the impact data.
    linalg::vec3f surface_normal = data.normal;
    (void)impact_position;
    (void)surface_normal;
    

    
}

} // namespace client::effects