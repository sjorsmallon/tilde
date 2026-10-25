#include "movement_kernel.hpp"
#include "hull_sweep.hpp"
#include "lighting/lens.hpp"
#include "player_constants.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace shared
{

namespace
{
// A face is ground when it leans no further than 45 degrees from up.
constexpr float GROUND_NORMAL_MINIMUM_Y = 0.707f;
} // namespace

vec3 clip_vector(vec3 in, vec3 normal, const float overbounce)
{
  // how strong is the incoming vector in the direction of the face normal?
  // (i.e. we split the incoming vector in two parts: the one that is parallel
  // to the normal, and the one that is perpendicular to it (along the wall).
  float backoff = dot(in, normal);

  if (backoff < 0.0f)
  {
    backoff *= overbounce;
  }
  else
  {
    backoff /= overbounce;
  }

  vec3 change = normal * backoff;

  vec3 result = in - change;

  return result;
}

vec3 clip_horizontal_speed(const vec3& velocity, const float speed_limit)
{
  const float speed = sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
  if (speed <= speed_limit)
    return velocity;

  const float scale = speed_limit / speed;
  return vec3{velocity.x * scale, velocity.y, velocity.z * scale};
}

namespace
{

// The step's displacement, capped in LENGTH by what the mover asked for. An
// infinite limit compares true on the first branch and hands the product back
// untouched, which is why every model pays nothing for this.
[[nodiscard]] vec3 limited_travel(const vec3& velocity, float dt, float travel_limit)
{
  const vec3  travel   = velocity * dt;
  const float distance = length(travel);
  if (distance <= travel_limit)
    return travel;
  return travel * (travel_limit / distance);
}

[[nodiscard]] bool stops_speed_as_a_wall(steep_face_rule_t steep_faces, const vec3& wall_normal)
{
  return steep_faces == steep_face_rule_t::Wall && wall_normal.y > 0.f;
}

[[nodiscard]] vec3 compute_flat_normal(const vec3& wall_normal)
{
  return normalize(vec3{wall_normal.x, 0.f, wall_normal.z});
}

[[nodiscard]] vec3 without_horizontal_speed_into(const vec3& velocity, const vec3& flat_normal)
{
  const float into_face = dot(velocity, flat_normal);
  if (into_face >= 0.f)
    return velocity;
  return velocity - flat_normal * into_face;
}

[[nodiscard]] float compute_support_radius(const vec3& normal, float half_width, float half_height)
{
  return half_width * fabsf(normal.x) + half_height * fabsf(normal.y) + half_width * fabsf(normal.z);
}

// The face a sweep stopped at, as the plane it lies in: the hull rests hull_sweep_skin off it.
[[nodiscard]] Plane contact_plane_for_hit(const hull_sweep_hit_t& hit, const vec3& resting_center,
                                          float half_width, float half_height)
{
  Plane face;
  face.normal = hit.normal;
  face.point  = resting_center -
               hit.normal * (compute_support_radius(hit.normal, half_width, half_height) + hull_sweep_skin);
  return face;
}

// Quake's PM_SlideMove clip: against each face met so far; a clip that pushes into an earlier
// face slides along the edge the two share; into a third, the corner stops it dead.
[[nodiscard]] vec3 clip_against_faces(vec3 vector, Span<const vec3> normals,
                                      Span<const float> overbounces)
{
  for (size_t i = 0; i < normals.count; ++i)
  {
    if (dot(vector, normals[i]) >= 0.f)
      continue;
    vector = clip_vector(vector, normals[i], overbounces[i]);
    for (size_t j = 0; j < normals.count; ++j)
    {
      if (j == i || dot(vector, normals[j]) >= 0.f)
        continue;
      const vec3  crease        = cross(normals[i], normals[j]);
      const float crease_length = length(crease);
      if (crease_length < 1e-4f)
        return vec3{0.f, 0.f, 0.f};
      const vec3 edge = crease * (1.f / crease_length);
      vector          = edge * dot(edge, vector);
      for (size_t k = 0; k < normals.count; ++k)
        if (k != i && k != j && dot(vector, normals[k]) < 0.f)
          return vec3{0.f, 0.f, 0.f};
    }
  }
  return vector;
}

struct swept_travel_t
{
  vec3               hull_center;
  vec3               velocity;
  std::vector<Plane> wall_planes;
  float              ground_impact_speed = 0.f;
};

// `travel` is the step's displacement and `end_velocity` the velocity the step ends with; under
// gravity the two differ by half a step of it, and both are clipped by every face met, as Quake
// clips its midpoint and end velocities. A ground face clips with no overbounce: the 0.1% it
// would leave pointing up is a rising velocity, and a rising hull is never grounded.
// The faces the hull TOUCHES when the step opens (`contacts`) clip before the first sweep, so a
// hull resting against a wall or a ramp is clipped the same however the tick is split; the
// faces the sweep meets join them.
[[nodiscard]] swept_travel_t sweep_travel(const movement_settings_t& settings,
                                          steep_face_rule_t steep_faces,
                                          const Bounding_Volume_Hierarchy& bvh,
                                          const predicted_world_t& world,
                                          const contacts_t& contacts, vec3 hull_center,
                                          vec3 end_velocity, vec3 travel)
{
  constexpr int MAX_BUMPS   = 4;
  const float   half_width  = settings.shared.half_width;
  const float   half_height = settings.shared.half_height;

  swept_travel_t     result{.hull_center = hull_center, .velocity = end_velocity};
  std::vector<vec3>  normals;
  std::vector<float> overbounces;
  // Under the Wall rule a steep face first takes the horizontal speed into it, as the vertical
  // wall beneath it would, OUTSIDE the crease logic: it and the face are one face, not an edge.
  std::vector<vec3>  flat_normals;

  const auto add_wall_or_ceiling = [&](const vec3& normal)
  {
    const bool is_ceiling = normal.y < -GROUND_NORMAL_MINIMUM_Y;
    if (!is_ceiling && stops_speed_as_a_wall(steep_faces, normal))
      flat_normals.push_back(compute_flat_normal(normal));
    normals.push_back(normal);
    overbounces.push_back(settings.shared.overbounce);
  };
  const auto clip_both = [&]
  {
    for (const vec3& flat_normal : flat_normals)
    {
      travel          = without_horizontal_speed_into(travel, flat_normal);
      result.velocity = without_horizontal_speed_into(result.velocity, flat_normal);
    }
    travel          = clip_against_faces(travel, normals, overbounces);
    result.velocity = clip_against_faces(result.velocity, normals, overbounces);
  };

  for (const Plane& plane : contacts.wall_planes)
    add_wall_or_ceiling(plane.normal);
  for (const Plane& plane : contacts.ceiling_planes)
    add_wall_or_ceiling(plane.normal);
  if (!normals.empty())
    clip_both();

  for (int bump = 0; bump < MAX_BUMPS && dot(travel, travel) > 0.f; ++bump)
  {
    const std::optional<hull_sweep_hit_t> hit =
        sweep_hull(bvh, world, result.hull_center, result.hull_center + travel, half_width, half_height);
    if (!hit)
    {
      result.hull_center = result.hull_center + travel;
      break;
    }

    result.hull_center = result.hull_center + travel * hit->t;
    travel             = travel * (1.f - hit->t);

    const bool is_ground  = hit->normal.y > GROUND_NORMAL_MINIMUM_Y;
    const bool is_ceiling = hit->normal.y < -GROUND_NORMAL_MINIMUM_Y;
    if (is_ground)
    {
      if (result.velocity.y < 0.f)
        result.ground_impact_speed = std::max(result.ground_impact_speed, -result.velocity.y);
      normals.push_back(hit->normal);
      overbounces.push_back(1.f);
    }
    else
    {
      if (!is_ceiling)
        result.wall_planes.push_back(contact_plane_for_hit(*hit, result.hull_center, half_width, half_height));
      add_wall_or_ceiling(hit->normal);
    }

    clip_both();
  }

  return result;
}

} // namespace

ground_under_step_t compute_ground_under_step(const contacts_t& contacts, bool grounded, float vertical_velocity)
{
  const bool walking        = grounded && vertical_velocity <= 0.f;
  const bool leaving_ground = grounded && !walking;
  return {.walking    = walking,
          .has_ground = contacts.has_ground() && !leaving_ground,
          .normal     = leaving_ground ? vec3{0.f, 1.f, 0.f} : contacts.ground_normal()};
}

shared::aabb_bounds_t hull_aabb(const vec3& center, float half_width, float half_height)
{
  return {center - vec3{half_width, half_height, half_width},
          center + vec3{half_width, half_height, half_width}};
}

void collect_collision_candidates(const Bounding_Volume_Hierarchy& bvh,
                                  const predicted_world_t& world,
                                  const shared::aabb_bounds_t& bounds,
                                  std::vector<collision_candidate_t>& out)
{
  std::vector<const BVH_Primitive*> overlapping;
  bvh_intersect_aabb(bvh, bounds, overlapping);
  for (const BVH_Primitive* primitive : overlapping)
  {
    const uint8_t state = get_geometry_state_by_collision_id(world.disabled_geometry, primitive->id);
    if (state == GEOMETRY_SOLID_WHERE_LIT)
    {
      const shared::aabb_bounds_t contact = shared::intersection_aabb(bounds, primitive->aabb);
      if (!shared::any_reveal_cone_touches_box(world.reveal_cones, contact))
        continue;
    }
    else if (state == GEOMETRY_SOLID_UNLESS_ERASED)
    {
      const shared::aabb_bounds_t contact = shared::intersection_aabb(bounds, primitive->aabb);
      if (shared::any_erase_cone_contains_box(world.reveal_cones, contact))
        continue;
    }
    else if (state == GEOMETRY_SOLID_IN_SHADOW)
    {
      const shared::aabb_bounds_t contact = shared::intersection_aabb(bounds, primitive->aabb);
      if (!shared::any_shadow_volume_touches_box(world.shadow_volumes, contact))
        continue;
    }
    else if (state == GEOMETRY_SOLID_UNLESS_SHADOWED)
    {
      const shared::aabb_bounds_t contact = shared::intersection_aabb(bounds, primitive->aabb);
      if (shared::any_shadow_volume_contains_box(world.shadow_volumes, contact))
        continue;
    }
    else if (const std::optional<entities::Lens_Filter> filter = try_get_lens_filter_for_geometry_state(state))
    {
      const shared::aabb_bounds_t contact = shared::intersection_aabb(bounds, primitive->aabb);
      if (!shared::any_lens_watcher_sees_point(world.lens_watchers, world.lenses, *filter,
                                               shared::get_aabb_center(contact)))
        continue;
    }
    else if (state != GEOMETRY_SOLID)
    {
      continue;
    }
    out.push_back({&primitive->collision_planes, &primitive->face_polygons,
                   shared::null_entity_uid, primitive->id});
  }

  for (const shared::mover_t& mover : world.movers)
  {
    if (!shared::aabbs_intersect(bounds, mover.swept_bounds))
      continue;
    for (const shared::collision_piece_t& piece : mover.pieces)
      if (shared::aabbs_intersect(bounds, piece.bounds))
        out.push_back({&piece.planes, &piece.face_polygons, mover.uid,
                       Collision_Id{Collision_Id::Type::Entity, mover.uid}});
  }
}

float hull_penetration_depth(const std::vector<Plane>& planes, const vec3& center,
                             float half_width, float half_height)
{
  if (planes.empty())
    return 0.f;

  float depth = std::numeric_limits<float>::infinity();
  for (const Plane& plane : planes)
  {
    const float support_radius = half_width * fabsf(plane.normal.x) +
                                 half_height * fabsf(plane.normal.y) +
                                 half_width * fabsf(plane.normal.z);
    depth = std::min(depth, support_radius - dot(center - plane.point, plane.normal));
  }
  return depth;
}

// The pieces the hull TOUCHES or is inside of: for each, the face it is least deep behind, as a
// contact plane. A hull inside, or nearer than the skin, is pushed out to hull_sweep_skin off
// that face, where a sweep would have left it.
//
// `debug_faces` is the caller's bucket, already gated on debug_show_collisions by
// the caller: the recording happens in SHARED code but the drawing is
// client-side, so both the flag and the destination have to arrive from
// whichever side is simulating rather than from a global. Null means the caller
// has no reader for them (the server, every time) -- see debug_collision.hpp.
contacts_t resolve_collisions(const movement_settings_t& settings, steep_face_rule_t steep_faces,
                              const Bounding_Volume_Hierarchy& bvh,
                              const predicted_world_t& world, vec3& player_pos,
                              debug_collision::Face_Bucket* debug_faces)
{
  const float half_width  = settings.shared.half_width;
  const float half_height = settings.shared.half_height;

  contacts_t result;
  constexpr float cos_45 = GROUND_NORMAL_MINIMUM_Y;

  shared::aabb_bounds_t player_aabb = hull_aabb(player_pos, half_width, half_height);

  std::vector<collision_candidate_t> overlapping;
  collect_collision_candidates(bvh, world, player_aabb, overlapping);

  for (const collision_candidate_t& candidate : overlapping)
  {
    const std::vector<Plane>& collision_planes = *candidate.collision_planes;
    if (collision_planes.empty())
      continue;

    // Rebuild player AABB from (potentially updated) player_pos each iteration
    player_aabb = hull_aabb(player_pos, half_width, half_height);

    // Hull-plane penetration test:
    // For each plane of the convex hull, compute how far the player AABB
    // penetrates past it. If the player is fully outside any face, it's
    // not inside the hull. Otherwise, push out along the least-penetrated face.
    float min_penetration = -1e30f;
    float best_biased_penetration = -1e30f;
    int min_plane_idx = -1;
    vec3 push_normal = {0, 0, 0};
    bool outside = false;

    for (int pi = 0; pi < (int)collision_planes.size(); ++pi)
    {
      const auto &plane = collision_planes[pi];
      float signed_dist = dot(player_pos - plane.point, plane.normal);
      // Support radius: how far the AABB extends along the plane normal direction
      float support_radius = half_width * fabsf(plane.normal.x) +
                              half_height * fabsf(plane.normal.y) +
                              half_width * fabsf(plane.normal.z);
      float penetration = signed_dist - support_radius;

      // Outside this face by more than the band: not touching the piece. Within it, the hull
      // TOUCHES: a resting hull sits hull_sweep_skin off its face and the band must hold it.
      if (penetration >= 2.f * hull_sweep_skin)
      {
        outside = true;
        break;
      }

      // At ledge corners the side face often has slightly less penetration
      // than the top face, so the resolver pushes sideways (wall) instead of
      // upward (ground). Bias toward upward-facing normals so the player
      // lands on top rather than bouncing off the side.
      bool is_ground_normal = plane.normal.y > cos_45;
      float biased_penetration = penetration;
      if (is_ground_normal)
        biased_penetration += 8.f; // make ground faces win when close

      if (biased_penetration > best_biased_penetration)
      {
        best_biased_penetration = biased_penetration;
        min_penetration         = penetration;
        push_normal = plane.normal;
        min_plane_idx = pi;
      }
    }

    if (outside)
      continue;

    // Push the hull out to where a sweep would have stopped it, hull_sweep_skin off the face. The
    // ground and the walls are the sweep's business from there.
    const float push_amount = -min_penetration + hull_sweep_skin;
    if (push_amount > 0.f)
    {
      // Out of a wall-ruled steep face the hull leaves sideways, or walking into its foot lifts it.
      if (push_normal.y <= cos_45 && stops_speed_as_a_wall(steep_faces, push_normal))
      {
        const vec3 flat_normal = compute_flat_normal(push_normal);
        player_pos = player_pos + flat_normal * (push_amount / dot(flat_normal, push_normal));
      }
      else
      {
        player_pos = player_pos + push_normal * push_amount;
      }
    }

    // Create a collision plane at the contact point
    Plane p;
    p.normal = push_normal;
    p.point = player_pos - push_normal * 0.01f;

    // Record collision for debug visualization
    if (debug_faces && min_plane_idx >= 0 &&
        min_plane_idx < (int)candidate.face_polygons->size())
      debug_collision::record_collision(*debug_faces, p,
                                        (*candidate.face_polygons)[min_plane_idx]);

    // Classify: ground (normal pointing up), ceiling (down), wall (horizontal)
    if (push_normal.y > cos_45)
    {
      if (result.ground_planes.empty() && min_plane_idx >= 0)
        result.ground_contact = {.id        = candidate.id,
                                 .mover_uid = candidate.mover_uid,
                                 .plane     = collision_planes[min_plane_idx]};
      result.ground_planes.push_back(p);
      if (result.ground_mover_uid == shared::null_entity_uid)
        result.ground_mover_uid = candidate.mover_uid;
    }
    else if (push_normal.y < -cos_45)
    {
      result.ceiling_planes.push_back(p);
    }
    else
    {
      result.wall_planes.push_back(p);
    }
  }

  return result;
}

std::optional<ground_below_t> find_ground_below(const movement_settings_t& settings,
                                                const Bounding_Volume_Hierarchy& bvh,
                                                const predicted_world_t& world,
                                                const vec3& hull_center, float reach)
{
  const float half_width  = settings.shared.half_width;
  const float half_height = settings.shared.half_height;

  const std::optional<hull_sweep_hit_t> hit = sweep_hull(
      bvh, world, hull_center, hull_center - vec3{0.f, reach, 0.f}, half_width, half_height);
  if (!hit || hit->normal.y <= GROUND_NORMAL_MINIMUM_Y)
    return std::nullopt;

  const float distance       = reach * hit->t;
  const vec3  resting_center = hull_center - vec3{0.f, distance, 0.f};
  const Plane face           = contact_plane_for_hit(*hit, resting_center, half_width, half_height);

  return ground_below_t{.distance = distance,
                        .contact  = {.id = hit->id, .mover_uid = hit->mover_uid, .plane = face}};
}

contacts_t sense_contacts(const movement_settings_t& settings, steep_face_rule_t steep_faces,
                          const Bounding_Volume_Hierarchy& bvh, const predicted_world_t& world,
                          vec3& hull_center, debug_collision::Face_Bucket* debug_faces)
{
  contacts_t contacts =
      resolve_collisions(settings, steep_faces, bvh, world, hull_center, debug_faces);

  contacts.ground_planes.clear();
  contacts.ground_contact   = {};
  contacts.ground_mover_uid = null_entity_uid;
  if (const std::optional<ground_below_t> ground =
          find_ground_below(settings, bvh, world, hull_center, ground_probe_distance))
  {
    contacts.ground_planes.push_back(ground->contact.plane);
    contacts.ground_contact   = ground->contact;
    contacts.ground_mover_uid = ground->contact.mover_uid;
  }
  return contacts;
}

slide_result_t slide(const movement_settings_t& settings, steep_face_rule_t steep_faces,
                     const Bounding_Volume_Hierarchy& bvh, const predicted_world_t& world,
                     const contacts_t& contacts, bool grounded, const wanted_move_t& wanted,
                     const vec3& hull_center, const float dt)
{
  const float               overbounce = settings.shared.overbounce;
  const ground_under_step_t frame = compute_ground_under_step(contacts, grounded, wanted.vertical_velocity);

  // Clip the wanted velocity into the ground's frame and give it back its speed: on a slope the
  // wish walks along the surface at the speed it asked for.
  vec3        new_velocity = wanted.velocity;
  const float new_speed    = length(new_velocity);
  new_velocity             = clip_vector(new_velocity, frame.normal, overbounce);
  new_velocity             = normalize(new_velocity) * new_speed;

  if (frame.walking)
  {
    new_velocity = clip_horizontal_speed(new_velocity, wanted.horizontal_speed_limit);

    swept_travel_t swept = sweep_travel(settings, steep_faces, bvh, world, contacts, hull_center,
                                        new_velocity, limited_travel(new_velocity, dt, wanted.travel_limit));

    // The ground clip above gave the velocity its y so the travel followed the slope; it is not
    // kept, or the next step would read it as a rise or a fall.
    if (frame.has_ground)
      swept.velocity.y = 0.f;

    // A walking hull meeting a ground face changed slope; only a flying one lands.
    return {.hull_center = swept.hull_center,
            .velocity    = swept.velocity,
            .wall_planes = std::move(swept.wall_planes)};
  }

  // The travel integrates with the step's MIDPOINT y velocity, which under a constant g is the
  // exact parabola, the one answer that does not move when dt does; the step ends with the full
  // step of gravity. See player_move_step_invariance_test.
  const float gravity_step = wanted.gravity * dt;

  // The limit bounds what the MODEL added, so it is applied before the faces see the vector: a
  // ramp turning gravity into speed is not the model's push.
  new_velocity = clip_horizontal_speed(new_velocity, wanted.horizontal_speed_limit);
  vec3 midpoint_velocity = new_velocity;
  midpoint_velocity.y    = wanted.vertical_velocity - 0.5f * gravity_step;
  vec3 end_velocity      = new_velocity;
  end_velocity.y         = wanted.vertical_velocity - gravity_step;

  // In the air the WHOLE vector is clipped against every face met, as one vector and with no
  // rescale, Source's TryPlayerMove: a vertical wall never touches the vertical, a ramp steeper
  // than 45 degrees turns the fall into along-ramp speed and a sideways push into lift, which IS
  // surfing.
  const swept_travel_t swept =
      sweep_travel(settings, steep_faces, bvh, world, contacts, hull_center, end_velocity,
                   limited_travel(midpoint_velocity, dt, wanted.travel_limit));

  return {.hull_center         = swept.hull_center,
          .velocity            = swept.velocity,
          .wall_planes         = std::move(swept.wall_planes),
          .ground_impact_speed = swept.ground_impact_speed};
}

settled_move_t settle_move(const movement_settings_t& settings, const Bounding_Volume_Hierarchy& bvh,
                           const predicted_world_t& world, const slide_result_t& slid,
                           float ground_reach)
{
  vec3 hull_center = slid.hull_center;
  vec3 velocity    = slid.velocity;

  // A rising hull is never on the ground, whatever is below: a jump's first step leaves the
  // floor on purpose. A hull within reach is set down on the face, and the downward speed it
  // arrived with, taken by the slide's ground clip or still in the velocity, is the landing.
  const std::optional<ground_below_t> ground =
      velocity.y <= 0.f ? find_ground_below(settings, bvh, world, hull_center, ground_reach)
                        : std::nullopt;
  float land_impact_speed = 0.f;
  if (ground)
  {
    hull_center.y -= ground->distance;
    land_impact_speed = std::max(slid.ground_impact_speed, -velocity.y);
    velocity.y        = 0.f;
  }

  return {.hull_center       = hull_center,
          .velocity          = velocity,
          .grounded          = ground.has_value(),
          .ground_mover_uid  = ground ? ground->contact.mover_uid : null_entity_uid,
          .ground_contact    = ground ? ground->contact : ground_contact_t{},
          .land_impact_speed = land_impact_speed};
}

namespace
{
[[nodiscard]] float ground_distance_between(const vec3& from, const vec3& to)
{
  const float dx = to.x - from.x;
  const float dz = to.z - from.z;
  return std::sqrt(dx * dx + dz * dz);
}
} // namespace

std::optional<slide_result_t> try_stair_step(const movement_settings_t& settings,
                                             steep_face_rule_t steep_faces,
                                             const Bounding_Volume_Hierarchy& bvh,
                                             const predicted_world_t& world,
                                             const contacts_t& contacts, const wanted_move_t& wanted,
                                             const slide_result_t& slid, const vec3& hull_center,
                                             const float dt)
{
  bool pushing_into_a_wall = false;
  for (const Plane& wall : slid.wall_planes)
    pushing_into_a_wall |= dot(wanted.velocity, wall.normal) < 0.f;
  if (!pushing_into_a_wall)
    return std::nullopt;

  const float half_width  = settings.shared.half_width;
  const float half_height = settings.shared.half_height;
  const float step_height = settings.shared.step_height;

  const vec3                            up{0.f, step_height, 0.f};
  const std::optional<hull_sweep_hit_t> ceiling =
      sweep_hull(bvh, world, hull_center, hull_center + up, half_width, half_height);
  const float rise = ceiling ? step_height * ceiling->t : step_height;
  if (rise <= 0.f)
    return std::nullopt;
  const vec3 raised_center = hull_center + vec3{0.f, rise, 0.f};

  // The walls the step opened against are what it is stepping over; one still there at the
  // raised height is met again by the sweep, a hair into the move.
  contacts_t raised_contacts = contacts;
  raised_contacts.wall_planes.clear();
  slide_result_t stepped =
      slide(settings, steep_faces, bvh, world, raised_contacts, true, wanted, raised_center, dt);

  const vec3                            down{0.f, -rise, 0.f};
  const std::optional<hull_sweep_hit_t> landing = sweep_hull(
      bvh, world, stepped.hull_center, stepped.hull_center + down, half_width, half_height);
  if (landing && landing->normal.y <= GROUND_NORMAL_MINIMUM_Y)
    return std::nullopt;
  stepped.hull_center = stepped.hull_center + down * (landing ? landing->t : 1.f);

  if (ground_distance_between(hull_center, stepped.hull_center) <=
      ground_distance_between(hull_center, slid.hull_center))
    return std::nullopt;

  return stepped;
}

volume_touch_t touch_movement_volumes(const movement_settings_t& settings,
                                      Span<const movement_volume_t> volumes, move_state_t& state)
{
  const shared::aabb_bounds_t hull_after_move = shared::player_hull_bounds(
      state.feet, settings.shared.half_width, settings.shared.half_height);

  volume_touch_t touch{};
  bool           touching_a_volume = false;

  for (const movement_volume_t& volume : volumes)
  {
    if (!volume.enabled)
      continue;
    if (!shared::aabbs_intersect(hull_after_move, volume.bounds))
      continue;

    touching_a_volume = true;

    if (volume.uid == state.movement.pad_contact_uid)
      continue;

    switch (volume.kind)
    {
      case movement_volume_kind_t::Jump_Pad:
        apply_impulse(settings, state,
                      {.horizontal = impulse_mode_t::Set,
                       .vertical   = impulse_mode_t::Set,
                       .velocity   = volume.launch_velocity});
        break;
      case movement_volume_kind_t::Bounce:
        apply_impulse(settings, state,
                      {.horizontal = impulse_mode_t::Keep,
                       .vertical   = impulse_mode_t::Set,
                       .velocity   = volume.launch_velocity});
        break;
    }
    state.movement.pad_contact_uid = volume.uid;
    touch = {.launched = true, .uid = volume.uid, .kind = volume.kind};
    break;
  }

  if (!touching_a_volume)
    state.movement.pad_contact_uid = shared::null_entity_uid;

  return touch;
}

} // namespace shared
