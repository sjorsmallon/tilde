#include "movement_kernel.hpp"
#include "locomotion.hpp"
#include "player_constants.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace shared
{

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

ground_frame_t ground_frame_of(const contacts_t& contacts, bool grounded, float vertical_velocity)
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
  bvh_intersect_aabb(bvh, bounds, overlapping, world.disabled_geometry);
  for (const BVH_Primitive* primitive : overlapping)
    out.push_back({&primitive->collision_planes, &primitive->face_polygons});

  for (const shared::mover_t& mover : world.movers)
  {
    if (!shared::aabbs_intersect(bounds, mover.swept_bounds))
      continue;
    for (const shared::collision_piece_t& piece : mover.pieces)
      if (shared::aabbs_intersect(bounds, piece.bounds))
        out.push_back({&piece.planes, &piece.face_polygons, mover.uid});
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

// Resolve collisions against the BVH using hull-plane-based penetration test.
// Pushes player_pos out of overlapping hulls and classifies contact normals.
//
// `debug_faces` is the caller's bucket, already gated on debug_show_collisions by
// the caller: the recording happens in SHARED code but the drawing is
// client-side, so both the flag and the destination have to arrive from
// whichever side is simulating rather than from a global. Null means the caller
// has no reader for them (the server, every time) -- see debug_collision.hpp.
contacts_t resolve_collisions(const movement_settings_t& settings,
                              const Bounding_Volume_Hierarchy& bvh,
                              const predicted_world_t& world, vec3& player_pos,
                              debug_collision::Face_Bucket* debug_faces)
{
  const float half_width  = settings.shared.half_width;
  const float half_height = settings.shared.half_height;

  contacts_t result;
  constexpr float cos_45 = 0.707f;

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

      if (penetration >= 0.f)
      {
        // Player is fully outside this face -> not inside the hull
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

      if (biased_penetration > min_penetration)
      {
        min_penetration = penetration; // store actual penetration for push amount
        push_normal = plane.normal;
        min_plane_idx = pi;
      }
    }

    if (outside)
      continue;

    // Push player out along the least-penetrated face, but keep a small
    // skin width of penetration. Without this, the player lands exactly at
    // the surface (penetration = 0), which the penetration test reads as
    // "outside" next frame — so ground contact is never detected. Leaving
    // a tiny margin ensures the next frame's test finds penetration < 0
    // and properly classifies the contact (ground/wall/ceiling).
    constexpr float skin_width = 0.01f;
    float push_amount = -min_penetration - skin_width;
    if (push_amount > 0.f)
      player_pos = player_pos + push_normal * push_amount;

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

slide_result_t slide(const movement_settings_t& settings, const contacts_t& contacts,
                     bool grounded, const wanted_move_t& wanted, const vec3& hull_center,
                     const float dt)
{
  const float          overbounce = settings.shared.overbounce;
  const ground_frame_t frame = ground_frame_of(contacts, grounded, wanted.vertical_velocity);
  constexpr auto       world_down = vec3{0.f, -1.f, 0.f};

  // clip the new velocity against the ground plane. take the length before
  // it is clipped.
  vec3  new_velocity = wanted.velocity;
  float new_speed    = length(new_velocity);
  new_velocity = clip_vector(new_velocity, frame.normal, overbounce);

  // since we take the velocity before clipping. it can be we clip the movement
  // vectors (effectively reducing player speed.) but we still want to retain
  // the speed we were moving in before.
  new_velocity = normalize(new_velocity);
  new_velocity = new_speed * new_velocity;

  // readjust the velocity for all the wall collider planes.
  for (const Plane& collider_plane : contacts.wall_planes)
  {
    // we should not collide with the plane if we are trying to move away from
    // it.
    new_speed = length(new_velocity);
    new_velocity = normalize(new_velocity);
    if (dot(new_velocity, collider_plane.normal) > 0.f)
    {
      new_velocity = new_velocity * new_speed;
      continue;
    }

    new_velocity = clip_vector(new_velocity, collider_plane.normal, overbounce);

    // Speed-preserving rescale: when sliding along a wall we normalize and
    // rescale to new_speed so that touching a wall doesn't bleed speed.
    // BUT: new_velocity was a unit vector entering clip_vector, so when
    // pressing nearly perpendicular into a wall the clip leaves only a tiny
    // residual (< 0.01) pointing slightly away. Normalizing that to a unit
    // vector and rescaling to new_speed launches the player backward at full
    // speed every frame — causing an oscillation that friction eventually
    // snaps to zero. Guard: only rescale when the tangential component is
    // meaningful. A tiny residual is left as-is; friction zeroes it next frame.
    {
      float clip_len = length(new_velocity);
      if (clip_len > 0.01f)
        new_velocity = (new_velocity * (1.0f / clip_len)) * new_speed;
    }
  }

  if (frame.walking)
  {
    new_velocity = clip_horizontal_speed(new_velocity, wanted.horizontal_speed_limit);

    // NOTE: integrate position BEFORE snapping Y velocity. This order matters!
    // On slopes, the ground clip gives velocity a Y component so the player
    // follows the surface. If we zeroed Y first, the position would move
    // purely horizontally — floating off the slope, losing ground contact, and
    // falling into air mode (which has no friction and no jump). The snap
    // afterward prevents Y from accumulating across frames.
    //
    // We snap ALL Y (not just negative) because the overbounce factor in
    // clip_vector can produce tiny positive Y on slopes too. Even a small
    // positive Y fails the grounded check (vel_y <= 0) next frame, kicking
    // the player into air mode where gravity builds up negative Y.
    const vec3 position = hull_center + (new_velocity * dt);

    if (frame.has_ground)
      new_velocity.y = 0.f;

    return {.hull_center = position, .velocity = new_velocity};
  }

  float new_y_velocity = wanted.vertical_velocity;

  // clip against the ceiling.
  if (contacts.has_ceiling())
  {
    auto cos_angle_plane_world_down = dot(contacts.ceiling_normal(), world_down);
    if (cos_angle_plane_world_down > 0.707f)
    {
      // if we were already moving down, it does not matter.
      new_y_velocity = (new_y_velocity < 0.f ? new_y_velocity : 0.f);
    }
  }

  // Apply gravity as two halves around the position integration. Position then
  // integrates with the step's MIDPOINT y velocity, and under a constant g the
  // midpoint IS the exact mean of the endpoints -- so this reproduces
  // p0 + v0*dt - 0.5*g*dt^2 rather than the -1.0 that integrating with the END
  // velocity gave. The returned velocity is unchanged (v0 - g*dt either way),
  // so the grounded check, the land snap and the post-move clips see what they
  // always did.
  //
  // The point is not the extra 5% of jump apex, it is that the exact parabola
  // is the one answer that does not move when dt does: the old form dropped
  // 1.0*g*dt^2 whole, 0.75 split in two, 0.625 in four. Sub-tick makes that
  // difference reachable. See player_move_step_invariance_test.
  const float half_gravity_step = 0.5f * wanted.gravity * dt;

  new_velocity.y = new_y_velocity - half_gravity_step;
  new_velocity   = clip_horizontal_speed(new_velocity, wanted.horizontal_speed_limit);

  // @FIXME: test if we can actually be at the new position (collide with the
  // environment and push back). we need to perform a new trace here to prevent
  // tunneling / getting stuck in the ground.
  const vec3 position = hull_center + (new_velocity * dt);

  new_velocity.y -= half_gravity_step;
  return {.hull_center = position, .velocity = new_velocity};
}

settled_move_t resolve_after_move(const movement_settings_t& settings,
                                  const Bounding_Volume_Hierarchy& bvh,
                                  const predicted_world_t& world, vec3 hull_center, vec3 velocity,
                                  debug_collision::Face_Bucket* debug_faces)
{
  // Post-move collision resolve: push position out of any geometry we
  // tunneled into, and correct velocity so it doesn't fight the surface.
  const contacts_t post = resolve_collisions(settings, bvh, world, hull_center, debug_faces);

  const float overbounce = settings.shared.overbounce;

  // Ground: the pre-move resolve uses a penetration test, so the player must
  // be *inside* geometry for has_ground to be true. But this resolve pushes
  // the player to exactly the surface (penetration = 0), which reads as
  // "outside" next frame — so the pre-move resolve won't detect ground.
  // Without this snap, the player enters air_move, gravity accumulates
  // unchecked (-3800+), and the post-move keeps pushing them back each frame
  // in an invisible free-fall loop.
  //
  // We do a plain Y=0 snap here, NOT clip_vector — overbounce would push
  // velocity slightly upward, which fails the grounded check (vel_y <= 0).
  // This snap firing on a downward velocity *is* a landing: the player came in
  // falling (air_move leaves new_vel.y negative) and the ground arrests it.
  // Walking/resting on flat ground never trips this — my_walk_move zeroes y, so
  // new_vel.y is already 0. The arrested speed is the impact magnitude.
  float land_impact_speed = 0.f;
  if (post.has_ground() && velocity.y < 0.f)
  {
    land_impact_speed = -velocity.y;
    velocity.y = 0.f;
  }

  for (const Plane& plane : post.ceiling_planes)
  {
    if (dot(velocity, plane.normal) < 0.f)
      velocity = clip_vector(velocity, plane.normal, overbounce);
  }
  for (const Plane& plane : post.wall_planes)
  {
    if (dot(velocity, plane.normal) < 0.f)
      velocity = clip_vector(velocity, plane.normal, overbounce);
  }

  return {.hull_center       = hull_center,
          .velocity          = velocity,
          .grounded          = post.has_ground(),
          .ground_mover_uid  = post.ground_mover_uid,
          .land_impact_speed = land_impact_speed};
}

// Stair-step glide: if grounded and pressing into a wall, try raising the
// player by pm_step_height and re-testing. If no wall at the raised height
// blocks our wish direction, the obstacle is short enough to step over.
// Walk from the raised position, then drop back down onto the surface.
stair_step_t try_stair_step(const movement_settings_t& settings,
                            const Bounding_Volume_Hierarchy& bvh, const predicted_world_t& world,
                            const contacts_t& contacts, const move_state_t& state,
                            const move_input_t& input, const vec3& hull_center,
                            const vec3& wish_direction, debug_collision::Face_Bucket* debug_faces)
{
  // Only proceed if we're actually pressing toward at least one wall.
  bool pressing_into_wall = false;
  for (const Plane& plane : contacts.wall_planes)
  {
    if (dot(wish_direction, plane.normal) < 0.f)
    {
      pressing_into_wall = true;
      break;
    }
  }

  if (!pressing_into_wall)
    return {};

  const float step_height = settings.shared.step_height;
  vec3        raised_position = hull_center + vec3{0.f, step_height, 0.f};
  const contacts_t raised =
      resolve_collisions(settings, bvh, world, raised_position, debug_faces);

  // Only abort if a raised wall specifically blocks our wish direction.
  // Walls from other nearby obstacles that we're not moving into are ignored.
  for (const Plane& plane : raised.wall_planes)
  {
    if (dot(wish_direction, plane.normal) < 0.f)
      return {};
  }

  if (raised.has_ceiling())
    return {};

  // old_velocity may be near-zero: it was clipped against the wall that
  // triggered this step-up and then zeroed by friction. Reconstruct to
  // the intended running speed so the player carries momentum through
  // the step rather than shuffling across it at ~0 units/s.
  vec3 raised_velocity = vec3{state.velocity.x, 0.f, state.velocity.z};
  if (length(raised_velocity) < settings.shared.speed_threshold)
    raised_velocity = wish_direction * settings.shared.run_speed;

  move_state_t        raised_state = state;
  raised_state.velocity            = raised_velocity;
  const wanted_move_t wanted =
      decide_move(settings, raised, true, raised_velocity, raised_state, input);
  const slide_result_t stepped = slide(settings, raised, true, wanted, raised_position, input.dt);

  // Drop back down by step_height. resolve_collisions will push the
  // player up to sit on top of whatever surface is below (the step top,
  // or the original floor if we overshot).
  //
  // IMPORTANT: we also push extra forward by step_height in the wish
  // direction before dropping. Without this, the player's center is only
  // (velocity * dt) ≈ 5 units past the step edge. The step's side face
  // then has less absolute penetration than the top face, so
  // resolve_collisions pushes the player sideways (wall) instead of up
  // (ground). Adding step_height of extra horizontal offset guarantees
  // the top face always wins the SAT test regardless of step size.
  vec3 drop_position = stepped.hull_center;
  drop_position.x += wish_direction.x * step_height;
  drop_position.z += wish_direction.z * step_height;
  drop_position.y -= step_height;
  const contacts_t dropped =
      resolve_collisions(settings, bvh, world, drop_position, debug_faces);

  // Reject the step if a wall still blocks the wish direction at the
  // drop position — that means we ran into a real obstacle, not just
  // the edge of the stair we were climbing.
  for (const Plane& plane : dropped.wall_planes)
  {
    if (dot(wish_direction, plane.normal) < 0.f)
      return {};
  }

  if (!dropped.has_ground())
    return {};

  // Do not use the stepped velocity here. The velocity entering the step may
  // have been partially clipped by the wall (anywhere from 0..run_speed), so
  // it inherits that reduced speed after friction — giving the player a
  // visible slowdown on every step. The step bypasses the wall entirely, so
  // carry full speed in the wish direction instead.
  vec3 velocity = wish_direction * settings.shared.run_speed;
  velocity.y    = 0.f;

  return {.taken = true, .hull_center = drop_position, .velocity = velocity};
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
