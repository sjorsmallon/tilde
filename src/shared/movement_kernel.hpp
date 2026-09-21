#pragma once
#include "collision_detection.hpp"
#include "debug_collision.hpp"
#include "movement_settings.hpp"
#include "movement_volumes.hpp"
#include "plane.hpp"
#include "player_move.hpp"
#include "predicted_world.hpp"
#include <limits>
#include <vector>

// The half of a step that senses the world and slides the hull through it. It
// is one copy for every model: nothing in here reads a model's numbers, and
// nothing calls back up into one except the stair-step, which asks decide_move
// for the wanted move from the raised position.
namespace shared
{

struct contacts_t
{
  std::vector<Plane>   ground_planes;
  std::vector<Plane>   ceiling_planes;
  std::vector<Plane>   wall_planes;
  shared::entity_uid_t ground_mover_uid = shared::null_entity_uid;

  [[nodiscard]] bool has_ground() const { return !ground_planes.empty(); }
  [[nodiscard]] bool has_ceiling() const { return !ceiling_planes.empty(); }
  [[nodiscard]] vec3 ground_normal() const
  {
    return has_ground() ? ground_planes[0].normal : vec3{0.f, 1.f, 0.f};
  }
  [[nodiscard]] vec3 ceiling_normal() const
  {
    return has_ceiling() ? ceiling_planes[0].normal : vec3{0.f, -1.f, 0.f};
  }
};

// What a model or an override wants of this step, before anything is clipped.
struct wanted_move_t
{
  vec3  velocity               = {};
  float vertical_velocity      = 0.f;
  float horizontal_speed_limit = std::numeric_limits<float>::infinity();
  float gravity                = 0.f;
  // Caps the DISTANCE this step travels and never the speed, so a move that
  // arrives mid-step still ends at the speed it was going: what a reel flings
  // you with is its tunable rather than whatever fraction of a step was left.
  // Infinity is no cap, and is exact -- it moves no float.
  float travel_limit = std::numeric_limits<float>::infinity();
};

// The kernel WALKS a step when the hull is grounded and the wanted velocity
// does not rise, and FLIES it otherwise -- a jump is a velocity that rises. The
// ground a rising step leaves is no longer under it, which is why the frame the
// basis and the clip are taken in comes out of the same answer.
struct ground_frame_t
{
  bool walking    = false;
  bool has_ground = false;
  vec3 normal     = {0.f, 1.f, 0.f};
};

[[nodiscard]] ground_frame_t ground_frame_of(const contacts_t& contacts, bool grounded,
                                             float vertical_velocity);

[[nodiscard]] vec3 clip_vector(vec3 in, vec3 normal, float overbounce);
[[nodiscard]] vec3 clip_horizontal_speed(const vec3& velocity, float speed_limit);

struct collision_candidate_t
{
  const std::vector<Plane>*              collision_planes;
  const std::vector<std::vector<vec3f>>* face_polygons;
  shared::entity_uid_t                   mover_uid = shared::null_entity_uid;
};

[[nodiscard]] shared::aabb_bounds_t hull_aabb(const vec3& center, float half_width,
                                              float half_height);

void collect_collision_candidates(const Bounding_Volume_Hierarchy& bvh,
                                  const predicted_world_t& world,
                                  const shared::aabb_bounds_t& bounds,
                                  std::vector<collision_candidate_t>& out);

[[nodiscard]] float hull_penetration_depth(const std::vector<Plane>& planes, const vec3& center,
                                           float half_width, float half_height);

[[nodiscard]] contacts_t resolve_collisions(const movement_settings_t& settings,
                                            const Bounding_Volume_Hierarchy& bvh,
                                            const predicted_world_t& world, vec3& hull_center,
                                            debug_collision::Face_Bucket* debug_faces);

struct slide_result_t
{
  vec3 hull_center = {};
  vec3 velocity    = {};
};

[[nodiscard]] slide_result_t slide(const movement_settings_t& settings, const contacts_t& contacts,
                                   bool grounded, const wanted_move_t& wanted,
                                   const vec3& hull_center, float dt);

struct settled_move_t
{
  vec3                 hull_center       = {};
  vec3                 velocity          = {};
  bool                 grounded          = false;
  shared::entity_uid_t ground_mover_uid  = shared::null_entity_uid;
  float                land_impact_speed = 0.f;
  // The walls the velocity was clipped against, for a model whose memory must be clipped too.
  std::vector<Plane>   wall_planes;
};

[[nodiscard]] settled_move_t resolve_after_move(const movement_settings_t& settings,
                                                const Bounding_Volume_Hierarchy& bvh,
                                                const predicted_world_t& world, vec3 hull_center,
                                                vec3 velocity,
                                                debug_collision::Face_Bucket* debug_faces);

struct stair_step_t
{
  bool taken       = false;
  vec3 hull_center = {};
  vec3 velocity    = {};
};

[[nodiscard]] stair_step_t try_stair_step(const movement_settings_t& settings,
                                          const Bounding_Volume_Hierarchy& bvh,
                                          const predicted_world_t& world,
                                          const contacts_t& contacts, const move_state_t& state,
                                          const move_input_t& input, const vec3& hull_center,
                                          const vec3& wish_direction,
                                          debug_collision::Face_Bucket* debug_faces);

struct volume_touch_t
{
  bool                   launched = false;
  shared::entity_uid_t   uid      = shared::null_entity_uid;
  movement_volume_kind_t kind     = movement_volume_kind_t::Jump_Pad;
};

volume_touch_t touch_movement_volumes(const movement_settings_t& settings,
                                      Span<const movement_volume_t> volumes, move_state_t& state);

} // namespace shared
