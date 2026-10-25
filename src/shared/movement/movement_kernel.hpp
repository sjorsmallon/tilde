#pragma once
#include "collision/collision_detection.hpp"
#include "collision/debug_collision.hpp"
#include "ground_contact.hpp"
#include "movement_settings.hpp"
#include "movement_volumes.hpp"
#include "plane.hpp"
#include "player_move.hpp"
#include "collision/predicted_world.hpp"
#include <limits>
#include <optional>
#include <vector>

// The half of a step that senses the world and slides the hull through it. It
// is one copy for every model: nothing in here reads a model's numbers, and
// nothing calls back up into one.
namespace shared
{

struct contacts_t
{
  std::vector<Plane>   ground_planes;
  std::vector<Plane>   ceiling_planes;
  std::vector<Plane>   wall_planes;
  shared::entity_uid_t ground_mover_uid = shared::null_entity_uid;
  ground_contact_t     ground_contact;

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
struct ground_under_step_t
{
  bool walking    = false;
  bool has_ground = false;
  vec3 normal     = {0.f, 1.f, 0.f};
};

[[nodiscard]] ground_under_step_t compute_ground_under_step(const contacts_t& contacts, bool grounded,
                                             float vertical_velocity);

[[nodiscard]] vec3 clip_vector(vec3 in, vec3 normal, float overbounce);
[[nodiscard]] vec3 clip_horizontal_speed(const vec3& velocity, float speed_limit);

// What a face too steep to stand on does with horizontal speed pushed into it:
// a Ramp turns it into lift (surfing), a Wall stops it as the vertical wall under it would.
enum class steep_face_rule_t
{
  Ramp,
  Wall
};

struct collision_candidate_t
{
  const std::vector<Plane>*              collision_planes;
  const std::vector<std::vector<vec3f>>* face_polygons;
  shared::entity_uid_t                   mover_uid = shared::null_entity_uid;
  Collision_Id                           id        = {};
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
                                            steep_face_rule_t steep_faces,
                                            const Bounding_Volume_Hierarchy& bvh,
                                            const predicted_world_t& world, vec3& hull_center,
                                            debug_collision::Face_Bucket* debug_faces);

// How far below a resting hull the ground may still be under it: a sweep leaves the hull
// hull_sweep_skin above the face, the resolver leaves it a hair inside, a mover's carry a hair off.
constexpr float ground_probe_distance = 0.25f;

struct ground_below_t
{
  // Down from the hull centre to where the feet rest on the face; zero when touching.
  float            distance = 0.f;
  ground_contact_t contact;
};

// The ground under a hull is a SWEEP straight down, never a penetration: the first face within
// `reach`, when it is one the hull can stand on. A wall or a steep face first is no ground.
[[nodiscard]] std::optional<ground_below_t> find_ground_below(const movement_settings_t& settings,
                                                              const Bounding_Volume_Hierarchy& bvh,
                                                              const predicted_world_t& world,
                                                              const vec3& hull_center, float reach);

// The step's opening contacts: the resolver pushes a hull that starts inside something out and
// names the walls and ceilings it touches; the ground is find_ground_below's, within the probe.
[[nodiscard]] contacts_t sense_contacts(const movement_settings_t& settings,
                                        steep_face_rule_t steep_faces,
                                        const Bounding_Volume_Hierarchy& bvh,
                                        const predicted_world_t& world, vec3& hull_center,
                                        debug_collision::Face_Bucket* debug_faces);

struct slide_result_t
{
  vec3               hull_center = {};
  vec3               velocity    = {};
  // The walls the travel stopped at, in the order met: the stair step's gate, a model's memory clip.
  std::vector<Plane> wall_planes;
  // The downward speed a ground face took out of the velocity, the landing's impact.
  float              ground_impact_speed = 0.f;
};

// The hull travels its wanted move as a SWEEP: it stops at the first face, clips what is left of
// the move and the velocity against every face met so far, and goes on, four bumps at most. It
// never enters anything. A face the hull cannot stand on is a wall; under the Wall rule a steep
// one clips as the vertical wall beneath it and as itself.
[[nodiscard]] slide_result_t slide(const movement_settings_t& settings,
                                   steep_face_rule_t steep_faces,
                                   const Bounding_Volume_Hierarchy& bvh,
                                   const predicted_world_t& world, const contacts_t& contacts,
                                   bool grounded, const wanted_move_t& wanted,
                                   const vec3& hull_center, float dt);

struct settled_move_t
{
  vec3                 hull_center       = {};
  vec3                 velocity          = {};
  bool                 grounded          = false;
  shared::entity_uid_t ground_mover_uid  = shared::null_entity_uid;
  ground_contact_t     ground_contact;
  float                land_impact_speed = 0.f;
};

// `ground_reach` is how far below the moved hull the ground may be and still catch it: the
// probe for a flying step, step_height for a walking one, so a decline dropping away under a
// walking hull is walked down rather than flown off. A hull within reach is set down on it.
[[nodiscard]] settled_move_t settle_move(const movement_settings_t& settings,
                                         const Bounding_Volume_Hierarchy& bvh,
                                         const predicted_world_t& world,
                                         const slide_result_t& slid, float ground_reach);

// Quake's PM_StepSlideMove over a wall the ground slide stopped at: the hull sweeps up by
// step_height, slides the same wanted move from there, sweeps back down by what it rose, and
// the step is taken when it got further along the ground than the slide did. Three sweeps and
// no push: where the raised slide ends is where the hull comes down, on a tread of any depth.
[[nodiscard]] std::optional<slide_result_t> try_stair_step(const movement_settings_t& settings,
                                                           steep_face_rule_t steep_faces,
                                                           const Bounding_Volume_Hierarchy& bvh,
                                                           const predicted_world_t& world,
                                                           const contacts_t& contacts,
                                                           const wanted_move_t& wanted,
                                                           const slide_result_t& slid,
                                                           const vec3& hull_center, float dt);

struct volume_touch_t
{
  bool                   launched = false;
  shared::entity_uid_t   uid      = shared::null_entity_uid;
  movement_volume_kind_t kind     = movement_volume_kind_t::Jump_Pad;
};

volume_touch_t touch_movement_volumes(const movement_settings_t& settings,
                                      Span<const movement_volume_t> volumes, move_state_t& state);

} // namespace shared
