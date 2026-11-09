// Times the CPU side of the shadow volumes on a map, as one tick does it with no players and no movers: the cut
// (compute_shadow_volumes) and the solid shadows built from it.
//
// Usage: shadow_volume_bench <map-file> [iterations]

#include "assets/asset.hpp"
#include "assets/asset_state.hpp"
#include "entities/generated/entities/projected_image_entity_generated.hpp"
#include "entities/generated/entities/projector_entity_generated.hpp"
#include "lighting/projected_image.hpp"
#include "lighting/projector_mask.hpp"
#include "lighting/shadow_volume.hpp"
#include "lighting/solid_shadows.hpp"
#include "log.hpp"
#include "map/game_session.hpp"
#include "map/map.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

double median_milliseconds(std::vector<double> samples)
{
  std::sort(samples.begin(), samples.end());
  return samples.empty() ? 0.0 : samples[samples.size() / 2];
}

} // namespace

int main(int argc, char** argv)
{
  if (argc < 2 || argc > 3)
  {
    std::printf("usage: shadow_volume_bench <map-file> [iterations]\n");
    return 2;
  }
  const std::string map_path   = argv[1];
  const int         iterations = argc == 3 ? std::max(1, std::atoi(argv[2])) : 20;

  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::mount_asset_source();
  assets::init();

  const std::optional<shared::map_t> map = shared::try_load_map(map_path);
  if (!map)
  {
    std::printf("shadow_volume_bench: could not load '%s'\n", map_path.c_str());
    return 1;
  }
  shared::game_session_t session = shared::build_session(*map);

  std::vector<shared::shadow_volume_t> volumes;
  std::vector<shared::mover_t>         solid_movers;
  shared::shadow_volume_report_t       report;
  std::vector<double>                  cut_ms, solid_ms;
  for (int iteration = 0; iteration < iterations; ++iteration)
  {
    using clock = std::chrono::steady_clock;
    const clock::time_point start = clock::now();
    report = shared::compute_shadow_volumes(session.entity_system, session.bvh, session.owner_of, {}, {},
                                            session.mover_rests, session.projector_mask_cache, session.shadow_memo,
                                            volumes);
    const clock::time_point cut = clock::now();
    solid_movers.clear();
    shared::compute_solid_shadows(session.entity_system, volumes, session.shadow_memo, solid_movers);
    const clock::time_point solid = clock::now();
    cut_ms.push_back(std::chrono::duration<double, std::milli>(cut - start).count());
    solid_ms.push_back(std::chrono::duration<double, std::milli>(solid - cut).count());
  }

  std::printf("%s: %u casters under %u cutting lights, %u volumes kept (%u refused for too many planes)\n",
              map_path.c_str(), report.caster_pieces, report.cutting_lights, report.kept,
              report.refused_too_many_planes);
  std::printf("  %zu solid shadow movers\n", solid_movers.size());
  std::printf("%s\n", shared::describe_shadow_volume_report(report, volumes).c_str());
  std::printf("  median over %d: cut %.2f ms, solid shadows %.2f ms (a 60 Hz tick is 16.7 ms)\n", iterations,
              median_milliseconds(cut_ms), median_milliseconds(solid_ms));

  // The cut's two per-frame costs apart: the broad phase over every piece, and the glass pieces built for the
  // pictures' quads, each timed alone as the cut does it.
  std::vector<double> broad_ms, glass_ms;
  uint32_t            glass_pieces = 0;
  for (int iteration = 0; iteration < iterations; ++iteration)
  {
    using clock = std::chrono::steady_clock;
    const std::vector<shared::shadow_light_t> lights =
        shared::gather_shadow_lights(session.entity_system, {}, session.mover_rests);
    const clock::time_point start = clock::now();
    uint32_t                touched = 0;
    for (const shared::shadow_light_t& light : lights)
    {
      const shared::shadow_light_clip_t clip = shared::compute_shadow_light_clip(light);
      for (const BVH_Primitive& primitive : session.bvh.primitives)
        touched += shared::shadow_light_touches_box(light, clip, primitive.aabb) ? 1u : 0u;
    }
    const clock::time_point broad = clock::now();
    glass_pieces                  = 0;
    for (const shared::shadow_light_t& light : lights)
    {
      const entities::Projected_Image_Entity* image =
          entities::entity_as<entities::Projected_Image_Entity>(session.entity_system.try_find(light.image));
      const entities::Projector_Entity* projector =
          entities::entity_as<entities::Projector_Entity>(session.entity_system.try_find(light.uid));
      if (image == nullptr || projector == nullptr)
        continue;
      const shared::projector_mask_t& mask = shared::find_or_compute_projector_mask_for_image(
          session.projector_mask_cache, *image, projector->aspect);
      for (const shared::projector_mask_quad_t& quad : mask.quads)
        glass_pieces += shared::try_build_projector_mask_piece(light, quad).has_value() ? 1u : 0u;
    }
    const clock::time_point glass = clock::now();
    broad_ms.push_back(std::chrono::duration<double, std::milli>(broad - start).count());
    glass_ms.push_back(std::chrono::duration<double, std::milli>(glass - broad).count());
    (void)touched;
  }
  std::printf("  of which: broad phase over %zu pieces %.2f ms, %u glass pieces hulled %.2f ms\n",
              session.bvh.primitives.size(), median_milliseconds(broad_ms), glass_pieces,
              median_milliseconds(glass_ms));
  return 0;
}
