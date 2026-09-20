// Step invariance: one tick of `dt` versus N sub-steps of `dt/N`.
//
// This is step 2 of subtick_plan.md, and it is an INSTRUMENT before it is a
// guard. Sub-tick means a tick sometimes runs as two or more movement steps, so
// the question that has to be answerable is "what changes when a step is
// split". This file answers it by measurement, one cause at a time, and prints
// the numbers next to the theory that predicts them.
//
// Three outcomes are possible per scenario, and they are not the same kind of
// thing:
//
//   EXACT       the per-step function has a closed form that composes.
//               Friction's `exp(-k*dt)` is one, because exp(a)exp(b)=exp(a+b).
//   ARITHMETIC  a first-order approximation to something with a closed form.
//               Fixable. Gravity's position integral is the live one.
//   DELIBERATE  a clamp or a branch. Splitting changes the answer because the
//               clamp fires at a different moment, which IS the mechanism
//               (Quake air control). Asserted to still diverge, so that
//               "fixing" it fails here rather than in playtest.
//
// The scenarios are built so exactly one cause is live in each. Pure gravity
// runs against an EMPTY bvh with zero horizontal speed, because every other
// path in player_move -- the normalize/rescale round trip, the maxspeed clip,
// resolve_collisions -- perturbs the number by amounts that would otherwise
// have to be disentangled from the one being measured.
//
// Links game_shared alone: player_move takes a bvh and a cvar_state_t by
// reference and nothing else, so there is no context, no socket and no assets.
#include "../shared/collision_detection.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/map_geometry.hpp"
#include "../shared/player_move.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <tuple>
#include <vector>

using cvars::cvar_state_t;

static int failures = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failures;
}

static void check_near(float actual, float expected, float tolerance,
                       const char* what)
{
  const bool ok = std::fabs(actual - expected) <= tolerance;
  if (ok)
  {
    printf("  ok   %s  (%.6f, expected %.6f)\n", what, actual, expected);
  }
  else
  {
    printf("  FAIL %s  (%.6f, expected %.6f, off by %.6f > %.6f)\n", what,
           actual, expected, std::fabs(actual - expected), tolerance);
    ++failures;
  }
}

// --- the one knob ------------------------------------------------------------
//
// player_move.cpp integrates position with the END velocity of the step
// (semi-implicit Euler), so one step drops 1.0*g*dt^2 where the exact parabola
// drops 0.5. subtick_plan.md step 1 proposes `p += (v_before+v_after)*0.5*dt`,
// which is the trapezoid rule and therefore EXACT for a constant acceleration
// at any subdivision. It is not done: it is a feel change (jumps get ~0.11
// units/tick floatier at g_gravity 800, 60Hz), deliberately left as a call to
// make rather than a free fix.
//
// Flip this the moment that lands. Every gravity expectation below is derived
// from it, and the ARITHMETIC scenario turns into an EXACT one.
constexpr bool gravity_position_uses_endpoint_average = true;

// Coefficient of g*dt^2 in the position drop after N sub-steps of dt/N.
//
//   end velocity:  p_N = p0 + v0*dt - g*dt^2 * (N+1)/(2N)     -> 1, 3/4, 5/8 ...
//   trapezoid:     p_N = p0 + v0*dt - g*dt^2 * 1/2            -> exact, any N
//
// The first converges to the second from above as N grows, which is the whole
// problem in one line: subdividing a tick silently makes the player fall less.
static float expected_drop_coefficient(int sub_steps)
{
  if (gravity_position_uses_endpoint_average)
    return 0.5f;
  return (float)(sub_steps + 1) / (2.f * (float)sub_steps);
}

// --- fixtures ----------------------------------------------------------------

constexpr float tick_dt = 1.f / 60.f;
constexpr float half_width = 16.f;
constexpr float half_height = 36.f;

// +x, with +z as its right. Only perpendicularity matters here; player_move
// strips y from both and renormalizes.
static const vec3 look_front{1.f, 0.f, 0.f};
static const vec3 look_right{0.f, 0.f, 1.f};

static Bounding_Volume_Hierarchy empty_world()
{
  return build_bvh({});
}

// A floor whose top face is y = 0.
static Bounding_Volume_Hierarchy floor_world()
{
  const shared::geometry_value_t geometry =
      shared::make_box_brush({0.f, -64.f, 0.f}, {2048.f, 64.f, 2048.f});

  std::vector<BVH_Input> inputs;
  for (const shared::collision_piece_t &piece : shared::get_collision_pieces(geometry, 1))
  {
    BVH_Input input;
    input.aabb = piece.bounds;
    input.id = {Collision_Id::Type::Static_Geometry, 0};
    input.collision_planes = piece.planes;
    input.face_polygons = piece.face_polygons;
    inputs.push_back(std::move(input));
  }

  return build_bvh(inputs);
}

struct move_result_t
{
  vec3 position;
  vec3 velocity;
};

// What a run saw of the movement volumes it passed through.
struct pad_probe_t
{
  int  launches           = 0;
  vec3 velocity_at_launch = {};
};

// Where the reel let go, which is NOT where the tick ended: the release fires
// mid-tick and the player coasts the rest of it. Only the release itself is
// step-invariant, so only the release is what the pins below measure.
struct hook_probe_t
{
  int  releases         = 0;
  vec3 release_position = {};
  vec3 release_velocity = {};
};

// Run `total_dt` as `sub_steps` equal steps, feeding each step's output into
// the next -- exactly what a sub-tick split does.
//
// ONE Movement across the whole run, for the same reason position and velocity
// are threaded: it is per-player state that a tick's steps share, and a fresh
// one per step would hide precisely the step-count dependence this file exists
// to catch (a jump edge would re-fire on every sub-step).
static move_result_t run_split(const cvar_state_t& cvars,
                               const Bounding_Volume_Hierarchy& bvh,
                               const Move_Input& input, vec3 position,
                               vec3 velocity, float total_dt, int sub_steps,
                               entities::Movement* movement = nullptr,
                               Span<const shared::movement_volume_t> volumes = {},
                               pad_probe_t* out_pad = nullptr,
                               Span<const uint8_t> disabled_geometry = {},
                               Span<const shared::mover_t> movers = {},
                               hook_probe_t* out_hook = nullptr)
{
  const shared::predicted_world_t world{.disabled_geometry = disabled_geometry,
                                        .movement_volumes  = volumes,
                                        .movers            = movers};
  entities::Movement local_movement{};
  entities::Movement& state = movement != nullptr ? *movement : local_movement;

  const float step_dt = total_dt / (float)sub_steps;
  for (int i = 0; i < sub_steps; ++i)
  {
    Move_Events events{};
    std::tie(position, velocity) =
        player_move(cvars, input, state, bvh, world, position, velocity,
                    look_front,
                    look_right, aim_sweep_t{}, half_width, half_height, step_dt, &events);
    if (out_pad != nullptr && events.launched_by_pad)
    {
      ++out_pad->launches;
      // The velocity the LAUNCHING step ended with. The tick's FINAL velocity is
      // not the measurement: the sub-steps after the launch keep applying
      // gravity, so 64 of them legitimately arrive lower than one does.
      out_pad->velocity_at_launch = velocity;
    }
    if (out_hook != nullptr && events.hook_released)
    {
      ++out_hook->releases;
      out_hook->release_position = events.hook_release_position;
      out_hook->release_velocity = events.hook_release_velocity;
    }
  }
  return {position, velocity};
}

static float horizontal_speed(const vec3& v)
{
  return std::sqrt(v.x * v.x + v.z * v.z);
}

// --- 1. gravity: the velocity half is already exact ---------------------------
//
// v' = -g is a CONSTANT. Nothing is expressed in terms of itself, so `v -= g*dt`
// is not an approximation at all -- it is the closed form, and it composes for
// the same reason 3+3 = 2+2+2. Establishing this first is what isolates the
// position integral as the only thing wrong in the next test.
static void test_gravity_velocity_is_exact(const cvar_state_t& cvars)
{
  printf("\n[EXACT] gravity -> velocity: a constant derivative composes\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  const Move_Input input;
  const vec3 start_position{0.f, 1000.f, 0.f};
  const vec3 start_velocity{0.f, 0.f, 0.f};

  const float expected = -cvars.g_gravity * tick_dt;

  for (int sub_steps : {1, 2, 4, 16})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    printf("    N=%-2d  v.y = %.6f\n", sub_steps, result.velocity.y);
    // Loose enough to absorb N roundings of g*dt/N, tight enough that a real
    // scheme change (a factor of 2, a missing step) fails.
    check_near(result.velocity.y, expected, 1e-3f,
               "sub-stepping leaves the end velocity alone");
  }
}

// --- 2. gravity: the position half is first-order -----------------------------
//
// p' = v(t), and v is a straight line across the step. Using the END value is a
// right-hand rectangle rule; it misses the triangle. This is a QUADRATURE error,
// not the feedback error friction had -- position never influences velocity.
static void test_gravity_position_is_first_order(const cvar_state_t& cvars)
{
  printf("\n[%s] gravity -> position: integrating a ramp\n",
         gravity_position_uses_endpoint_average ? "EXACT" : "ARITHMETIC");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  const Move_Input input;
  const float start_height = 1000.f;
  const vec3 start_position{0.f, start_height, 0.f};
  const vec3 start_velocity{0.f, 0.f, 0.f};

  const float g_dt_squared = cvars.g_gravity * tick_dt * tick_dt;
  const float exact_drop = 0.5f * g_dt_squared;

  printf("    exact parabola drops %.6f units this tick\n", exact_drop);

  float previous_drop = 0.f;
  for (int sub_steps : {1, 2, 4, 16, 64})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    const float drop = start_height - result.position.y;
    const float expected = expected_drop_coefficient(sub_steps) * g_dt_squared;

    printf("    N=%-2d  drop %.6f  (%.4f * g*dt^2)  error vs exact %+.6f\n",
           sub_steps, drop, drop / g_dt_squared, drop - exact_drop);
    check_near(drop, expected, 1e-4f, "drop matches the predicted coefficient");

    // Monotone convergence toward the truth from above. This is the sentence
    // "more sub-steps means a floatier jump" written as an assertion.
    if (!gravity_position_uses_endpoint_average && previous_drop > 0.f)
      check(drop < previous_drop, "...and more sub-steps fall strictly less");
    previous_drop = drop;
  }

  const move_result_t one = run_split(cvars, bvh, input, start_position,
                                      start_velocity, tick_dt, 1);
  const float one_step_drop = start_height - one.position.y;
  if (gravity_position_uses_endpoint_average)
  {
    check_near(one_step_drop, exact_drop, 1e-4f,
               "one step reproduces the exact parabola");
  }
  else
  {
    check_near(one_step_drop - exact_drop, exact_drop, 1e-4f,
               "one step overshoots the parabola by exactly 0.5*g*dt^2");
  }
}

// --- 3. gravity is the ONLY axis at fault in the air --------------------------
//
// With no input there is no horizontal acceleration, so p' = v with v constant
// -- a rectangle rule over a flat integrand is exact. If this ever diverges, the
// cause is the normalize/rescale round trip or the maxspeed clip, not the
// integrator, and the test above stops being isolated.
static void test_air_horizontal_composes(const cvar_state_t& cvars)
{
  printf("\n[EXACT] air, no input: constant horizontal velocity integrates\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  const Move_Input input;
  const vec3 start_position{0.f, 1000.f, 0.f};
  const vec3 start_velocity{200.f, 0.f, 0.f};

  const float expected_x = start_position.x + start_velocity.x * tick_dt;

  for (int sub_steps : {1, 2, 8})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    printf("    N=%-2d  x = %.6f  vx = %.6f\n", sub_steps, result.position.x,
           result.velocity.x);
    check_near(result.position.x, expected_x, 1e-3f,
               "horizontal position is unaffected by the split");
    check_near(result.velocity.x, start_velocity.x, 1e-3f,
               "horizontal velocity is unaffected by the split");
  }
}

// --- 4. friction: the 2026-08-18 fix, finally guarded -------------------------
//
// v' = -k*v is the feedback case: the derivative depends on the state being
// updated, so the linear form composed WRONG (the second half-step was charged
// against an already-reduced speed). exp(-k*dt) composes because
// exp(a)exp(b) = exp(a+b). Until this file there was no test on it.
static void test_friction_speed_composes(const cvar_state_t& cvars)
{
  printf("\n[EXACT] ground friction above pm_stopspeed: exp() composes\n");

  const Bounding_Volume_Hierarchy bvh = floor_world();
  const Move_Input input;
  // Penetrating the floor by a hair: resolve_collisions is a penetration test,
  // and it pushes back out to a 0.01 skin so contact survives the next step.
  const vec3 start_position{0.f, -0.02f, 0.f};
  const vec3 start_velocity{300.f, 0.f, 0.f};

  const float expected_speed =
      horizontal_speed(start_velocity) * std::exp(-cvars.pm_friction * tick_dt);

  printf("    exp(-%.1f * %.5f) = %.6f, so %.1f -> %.6f\n", cvars.pm_friction,
         tick_dt, std::exp(-cvars.pm_friction * tick_dt),
         horizontal_speed(start_velocity), expected_speed);

  for (int sub_steps : {1, 2, 4, 16})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    const float speed = horizontal_speed(result.velocity);
    printf("    N=%-2d  speed = %.6f\n", sub_steps, speed);
    check_near(speed, expected_speed, 1e-2f,
               "sub-stepping leaves the decayed speed alone");
  }
}

// --- 5. friction below pm_stopspeed: the linear floor is exact too ------------
//
// Below pm_stopspeed the drop becomes the CONSTANT pm_stopspeed*friction*dt --
// it stops depending on the thing it is changing, so the feedback is gone and a
// linear step is the closed form. The branch must stay linear on purpose:
// exponential decay never reaches zero, and this is the deceleration floor.
static void test_friction_floor_composes(const cvar_state_t& cvars)
{
  printf("\n[EXACT] ground friction below pm_stopspeed: a constant drop\n");

  const Bounding_Volume_Hierarchy bvh = floor_world();
  const Move_Input input;
  const vec3 start_position{0.f, -0.02f, 0.f};
  const float start_speed = 0.5f * cvars.pm_stopspeed;
  const vec3 start_velocity{start_speed, 0.f, 0.f};

  const float expected_speed =
      start_speed - cvars.pm_stopspeed * cvars.pm_friction * tick_dt;

  for (int sub_steps : {1, 2, 8})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    const float speed = horizontal_speed(result.velocity);
    printf("    N=%-2d  speed = %.6f\n", sub_steps, speed);
    check_near(speed, expected_speed, 1e-2f,
               "the deceleration floor composes under a split");
  }
}

// --- 6. friction: position does NOT compose, and averaging will not fix it ----
//
// The caveat the gravity fix does not cover. `p += v_after*dt` is the same
// right-hand rectangle rule, but here v decays EXPONENTIALLY across the step, so
// the endpoint average is a second-order approximation rather than an exact
// one. Expect this to shrink when the trapezoid lands, not to vanish.
//
//   truth:  integral of v0*exp(-k t) over dt  =  v0*(1 - exp(-k*dt))/k
//   today:  h * sum_{i=1..N} v0*exp(-k*i*h),  h = dt/N   (geometric series)
static void test_ground_position_is_first_order(const cvar_state_t& cvars)
{
  printf("\n[ARITHMETIC] ground friction -> position: integrating a decay\n");

  const Bounding_Volume_Hierarchy bvh = floor_world();
  const Move_Input input;
  const vec3 start_position{0.f, -0.02f, 0.f};
  const float start_speed = 300.f;
  const vec3 start_velocity{start_speed, 0.f, 0.f};

  const float k = cvars.pm_friction;
  const float exact_distance = start_speed * (1.f - std::exp(-k * tick_dt)) / k;
  printf("    exact integral of the decay: %.6f units\n", exact_distance);

  float previous_distance = 0.f;
  for (int sub_steps : {1, 2, 4, 16, 64})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    const float distance = result.position.x - start_position.x;

    const float step_dt = tick_dt / (float)sub_steps;
    const float ratio = std::exp(-k * step_dt);
    const float series =
        ratio * (1.f - std::pow(ratio, (float)sub_steps)) / (1.f - ratio);
    const float expected = start_speed * step_dt * series;

    printf("    N=%-2d  distance %.6f  error vs exact %+.6f\n", sub_steps,
           distance, distance - exact_distance);
    check_near(distance, expected, 2e-3f,
               "distance matches the rectangle-rule series");

    // Undershoots, and converges upward -- the mirror of gravity's overshoot,
    // because here the end velocity is the SMALLER endpoint.
    check(distance < exact_distance,
          "...and the right-hand rectangle undershoots the true integral");
    if (previous_distance > 0.f)
      check(distance > previous_distance, "...and more sub-steps travel further");
    previous_distance = distance;
  }
}

// --- 7. accelerate + friction: solved together, so the split composes --------
//
// Not in subtick_plan.md's original list, and it belongs there. On the ground
// each step runs friction and THEN accelerate, and that ORDER is what a split
// perturbs: friction gets charged against speed the previous sub-step just
// added. It was the friction bug's shape one level up -- neither operator is
// inexact any more, but applying them alternately is not the same as solving
// them together.
//
// Fixed 2026-08-19. The two are one system, v' = -k*v + A*w, and it has a
// closed form:
//
//   exact:  v(dt) = v0*exp(-k*dt) + (A/k)*(1 - exp(-k*dt))   A = accel*W
//
// which is `v0*decay` -- what apply_friction already returned -- plus the SAME
// accelerate as before, integrating over (1-exp(-k*dt))/k instead of dt. So the
// fix is a duration, not a new operator: apply_friction hands back the weighted
// time alongside the decayed velocity, and two halves of it sum to the whole.
//
// The clamp survives this exactly. min(d*f + c, W) composed with itself is
// min(d*f^2 + c*f + c, W) = min(d*F + C, W) whichever side of W each half lands
// on, because c >= W*(1-f) whenever accel >= pm_friction -- so a saturated
// projection stays saturated rather than drifting under the split.
//
// The feel cost was taken deliberately, the same call gravity and friction
// made: a tick now gains A*(1-exp(-k*dt))/k where it gained A*dt, which at
// pm_friction 6 and 60Hz is 4.8% less acceleration through the transient. The
// clamp binds after ~7 ticks from a standstill, so nothing about top speed
// moves; only the ramp does.
//
// The wish speed is kept away from the clamp (start well under pm_maxspeed) so
// the number measured is the ordering alone.
static void test_ground_accelerate_composes(const cvar_state_t& cvars)
{
  printf("\n[EXACT] ground accelerate + friction: one system, one closed form\n");

  const Bounding_Volume_Hierarchy bvh = floor_world();
  Move_Input input;
  input.forward_pressed = true;

  const vec3 start_position{0.f, -0.02f, 0.f};
  const float start_speed = 100.f;
  const vec3 start_velocity{start_speed, 0.f, 0.f};

  const float k = cvars.pm_friction;
  const float acceleration_rate = cvars.pm_ground_acceleration * cvars.pm_maxspeed;
  const float decay = std::exp(-k * tick_dt);
  const float exact_speed =
      start_speed * decay + (acceleration_rate / k) * (1.f - decay);

  printf("    exact solution of v' = -%.1f*v + %.0f : %.6f\n", k,
         acceleration_rate, exact_speed);
  printf("    (the pre-fix alternating recurrence gave %.6f at N=1)\n",
         start_speed * decay + cvars.pm_ground_acceleration * tick_dt *
                                   cvars.pm_maxspeed);

  for (int sub_steps : {1, 2, 4, 16, 64})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    const float speed = horizontal_speed(result.velocity);

    printf("    N=%-2d  speed %.6f  error vs exact %+.6f\n", sub_steps, speed,
           speed - exact_speed);
    check_near(speed, exact_speed, 2e-2f,
               "the split reproduces the coupled closed form");
  }
}

// --- 7b. and it still saturates at wish_speed, at every subdivision ----------
//
// The other half of the claim above: the clamp composes through the new
// duration too. Run long enough to be pinned, and every N agrees on the pin.
static void test_ground_saturation_is_step_invariant(const cvar_state_t& cvars)
{
  printf("\n[EXACT] ground accelerate at the clamp: every split pins alike\n");

  const Bounding_Volume_Hierarchy bvh = floor_world();
  Move_Input input;
  input.forward_pressed = true;

  const vec3 start_position{0.f, -0.02f, 0.f};
  // Already at the clamp: friction pulls it down, accelerate restores it.
  const vec3 start_velocity{cvars.pm_maxspeed, 0.f, 0.f};

  for (int sub_steps : {1, 2, 4, 16})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    const float speed = horizontal_speed(result.velocity);
    printf("    N=%-2d  speed = %.6f\n", sub_steps, speed);
    check_near(speed, cvars.pm_maxspeed, 1e-2f,
               "a saturated projection stays pinned under any split");
  }
}

// --- 8. the maxspeed clip: a re-projection, and it must keep diverging --------
//
// The finding that reshapes subtick_plan.md items 3 and 4: `accelerate`'s clamp
// is NOT independently step-dependent. Its increment is `accel*h*wish_speed`,
// proportional to h, and `min(min(d + c, W) + c, W) == min(d + 2c, W)` -- a
// monotone increment through a min composes exactly. With a fixed wish direction
// the clamp is a TARGET, so every subdivision saturates at the same wish_speed.
//
// What actually re-projects the velocity between sub-steps is the maxspeed clip
// in step_air_move: above max(incoming speed, pm_maxspeed) it rescales the
// horizontal vector, so the DIRECTION going into the next accelerate depends on
// when the clip fired. Split finer and the lateral gain is preserved rather than
// being renormalized away in one lump.
//
// So there is one structural mechanism here, not two, and it lives in the clip.
// Asserted to still diverge: making it dt-exact deletes air control, and it
// should fail here rather than in playtest.
static void test_maxspeed_clip_diverges(const cvar_state_t& cvars)
{
  printf("\n[DELIBERATE] the maxspeed clip re-projects: lateral gain grows\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  // Overspeed along +x (a rocket jump's worth), wishing along +z. The clip
  // fires every sub-step; what changes is how much of the +z gain survives it.
  Move_Input input;
  input.right_pressed = true;

  const vec3 start_position{0.f, 1000.f, 0.f};
  const vec3 start_velocity{400.f, 0.f, 0.f};

  float previous_lateral = 0.f;
  for (int sub_steps : {1, 2, 4, 16})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    printf("    N=%-2d  vz = %.6f  |v_xz| = %.6f\n", sub_steps,
           result.velocity.z, horizontal_speed(result.velocity));
    check_near(horizontal_speed(result.velocity), horizontal_speed(start_velocity), 1e-2f,
               "the clip pins horizontal speed to the speed it came in with");
    if (previous_lateral > 0.f)
      check(result.velocity.z > previous_lateral,
            "a finer split keeps strictly more lateral gain -- do NOT 'fix' this");
    previous_lateral = result.velocity.z;
  }
}

// --- 9. accelerate's clamp alone, with no clip to re-project it ---------------
//
// The control for 8. Same input, but starting below pm_maxspeed so the clip
// never fires: the clamp is then the only nonlinearity in the path, and the
// result is step-invariant. This is the assertion that says the divergence
// above belongs to the clip, and it is what makes item 3 of subtick_plan.md
// wrong as written.
static void test_accelerate_clamp_alone_composes(const cvar_state_t& cvars)
{
  printf("\n[EXACT] accelerate's clamp with no clip: a monotone min composes\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  Move_Input input;
  input.right_pressed = true;

  const vec3 start_position{0.f, 1000.f, 0.f};
  const vec3 start_velocity{200.f, 0.f, 0.f};

  float first_lateral = 0.f;
  for (int sub_steps : {1, 2, 4, 16})
  {
    const move_result_t result = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    printf("    N=%-2d  vz = %.6f  |v_xz| = %.6f\n", sub_steps,
           result.velocity.z, horizontal_speed(result.velocity));
    check(horizontal_speed(result.velocity) < cvars.pm_maxspeed,
          "stayed under pm_maxspeed, so the clip never fired");
    if (first_lateral == 0.f)
      first_lateral = result.velocity.z;
    else
      check_near(result.velocity.z, first_lateral, 1e-3f,
                 "the clamp alone is step-invariant");
  }
}

// --- 10. the ability seam: an EDGE must not scale with the split -------------
//
// This is the test the Movement component exists to make possible, and it is
// the one that fails the moment somebody reads the jump LEVEL to spend a
// charge. player_move splits a tick into one step per sub-tick edge, so a held
// jump key is `jump_pressed == true` on every one of them -- 1 step or 16. A
// budget spent off the level therefore empties in proportion to how many edges
// happened to be in the tick, which is a movement rule that depends on how fast
// you were moving your mouse.
//
// The state is threaded through the whole run (one Movement, N steps), because
// a fresh one per step is exactly the bug and would make this pass vacuously.
static void test_air_jump_fires_once_per_press(const cvar_state_t& cvars)
{
  printf("\n[EXACT] air jump: an edge-triggered ability is step-invariant\n");

  cvar_state_t with_air_jumps = cvars;
  with_air_jumps.pm_air_jump_count = 1;
  with_air_jumps.pm_air_jump_speed = 270.f;

  const Bounding_Volume_Hierarchy bvh = empty_world();

  Move_Input holding_jump;
  holding_jump.jump_pressed = true;

  const vec3 start_position{0.f, 1000.f, 0.f};
  const vec3 start_velocity{0.f, 0.f, 0.f};

  for (int sub_steps : {1, 2, 8, 16})
  {
    entities::Movement movement{};
    const move_result_t result = run_split(with_air_jumps, bvh, holding_jump, start_position,
                                           start_velocity, tick_dt, sub_steps, &movement);

    printf("    N=%-2d  charges spent = %u  vy = %.6f\n", sub_steps,
           (unsigned)movement.air_jumps_used, result.velocity.y);

    check(movement.air_jumps_used == 1,
          "exactly one charge is spent however many sub-steps the tick had");
    check(result.velocity.y > 0.f,
          "the air jump actually left the player rising");
  }

  // And the budget is a budget: with the key still held on the next tick there
  // is no new edge, so nothing more is spent. Holding jump does not levitate.
  entities::Movement movement{};
  vec3 position = start_position;
  vec3 velocity = start_velocity;
  for (int tick = 0; tick < 4; ++tick)
  {
    const move_result_t result = run_split(with_air_jumps, bvh, holding_jump, position, velocity,
                                           tick_dt, 4, &movement);
    position = result.position;
    velocity = result.velocity;
  }
  check(movement.air_jumps_used == 1,
        "holding the key across four ticks still spends exactly one charge");

  // With the ability off -- which is the shipped default -- nothing is spent
  // and the airborne player is in free fall, exactly as before Movement existed.
  entities::Movement without{};
  const move_result_t disabled = run_split(cvars, bvh, holding_jump, start_position,
                                           start_velocity, tick_dt, 8, &without);
  check(without.air_jumps_used == 0 && disabled.velocity.y < 0.f,
        "pm_air_jump_count 0 spends nothing and leaves free fall untouched");
}

// --- movement volumes: a jump pad is an edge, like an air jump ----------------
//
// The launch used to be a server system's write after the move loop, which the
// client could not see. It is a step now, which means it has to obey the same
// rule every other ability in here does: the step that carries the hull in is
// the step that launches it, once, whatever the split. And because the velocity
// is a table lookup rather than an integration, "the same" here means BIT
// identical -- an approximation would be a mispredicted launch.
static shared::movement_volume_t pad_at(const vec3& center, const vec3& half_extents,
                                        const vec3& launch_velocity, bool enabled)
{
  shared::movement_volume_t volume;
  volume.uid             = 7;
  volume.kind            = shared::movement_volume_kind_t::Jump_Pad;
  volume.bounds          = {center - half_extents, center + half_extents};
  volume.enabled         = enabled;
  volume.launch_velocity = launch_velocity;
  return volume;
}

static void test_a_jump_pad_fires_once_per_contact(const cvar_state_t& cvars)
{
  printf("\n[EXACT] jump pad: one launch per contact, bit-identical at any split\n");

  const Bounding_Volume_Hierarchy bvh = floor_world();
  const vec3 launch{0.f, 900.f, 0.f};

  // Sitting ON the pad at the start, so every sub-step of the tick overlaps it
  // and a level-read would fire on all of them.
  const std::vector<shared::movement_volume_t> volumes = {
      pad_at({0.f, 8.f, 0.f}, {32.f, 8.f, 32.f}, launch, true)};

  float first_velocity_y = 0.f;
  for (int sub_steps : {1, 2, 8, 64})
  {
    entities::Movement movement{};
    pad_probe_t        pad{};
    run_split(cvars, bvh, Move_Input{}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, tick_dt, sub_steps,
              &movement, Span<const shared::movement_volume_t>(volumes), &pad);

    printf("    N=%-2d  launches = %d  vy at launch = %.9f  latch = %u\n", sub_steps,
           pad.launches, pad.velocity_at_launch.y, (unsigned)movement.pad_contact_uid);

    check(pad.launches == 1, "exactly one launch however many sub-steps the tick had");
    check(movement.pad_contact_uid == 7, "the latch names the pad that fired");

    if (sub_steps == 1)
      first_velocity_y = pad.velocity_at_launch.y;
    else
      check(pad.velocity_at_launch.y == first_velocity_y,
            "the launch velocity at that step is BIT identical to the single-step run");
  }

  // Still standing on it next tick: the latch holds, so nothing re-fires and
  // gravity is free to take the launch back.
  {
    entities::Movement movement{};
    pad_probe_t        pad{};
    move_result_t      result{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
    for (int tick = 0; tick < 4; ++tick)
      result = run_split(cvars, bvh, Move_Input{}, result.position, result.velocity, tick_dt, 4,
                         &movement, Span<const shared::movement_volume_t>(volumes), &pad);

    check(pad.launches == 1, "four ticks of standing on one pad is still one launch");
  }

  // Leaving the volume clears the latch, so walking back on fires again. Run
  // the away tick against an EMPTY list, which is what "not overlapping any
  // volume" looks like from inside the step.
  {
    entities::Movement movement{};
    pad_probe_t        pad{};

    run_split(cvars, bvh, Move_Input{}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, tick_dt, 4, &movement,
              Span<const shared::movement_volume_t>(volumes), &pad);
    check(movement.pad_contact_uid == 7, "on the pad, latched");

    run_split(cvars, bvh, Move_Input{}, {512.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, tick_dt, 4, &movement,
              {}, &pad);
    check(movement.pad_contact_uid == shared::null_entity_uid,
          "off every volume, the latch is cleared");

    run_split(cvars, bvh, Move_Input{}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, tick_dt, 4, &movement,
              Span<const shared::movement_volume_t>(volumes), &pad);
    check(pad.launches == 2, "stepping back on fires a second time");
  }
}

// The switch is replicated state and the volume carries it rather than being
// filtered out at collection -- so the step has to be the thing that honours
// it, and a disabled pad has to be indistinguishable from no pad at all.
static void test_a_disabled_jump_pad_is_passed_through(const cvar_state_t& cvars)
{
  printf("\n[EXACT] jump pad: a disabled pad launches nobody and latches nothing\n");

  const Bounding_Volume_Hierarchy bvh = floor_world();
  const std::vector<shared::movement_volume_t> disabled = {
      pad_at({0.f, 8.f, 0.f}, {32.f, 8.f, 32.f}, {0.f, 900.f, 0.f}, false)};

  entities::Movement movement{};
  pad_probe_t        pad{};
  const move_result_t on_the_disabled_pad =
      run_split(cvars, bvh, Move_Input{}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, tick_dt, 8, &movement,
                Span<const shared::movement_volume_t>(disabled), &pad);

  entities::Movement no_volumes_movement{};
  const move_result_t with_no_volumes =
      run_split(cvars, bvh, Move_Input{}, {0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, tick_dt, 8,
                &no_volumes_movement);

  check(pad.launches == 0, "a disabled pad fires nothing");
  check(movement.pad_contact_uid == shared::null_entity_uid,
        "and latches nothing, so re-enabling it fires on the next step");
  check(on_the_disabled_pad.velocity.y == with_no_volumes.velocity.y &&
            on_the_disabled_pad.position.y == with_no_volumes.position.y,
        "the step through it is bit-identical to a step through no volume at all");
}

// --- a switched-off brush is not there ---------------------------------------
//
// The geometry half of the same rule the pad's `enabled` obeys, and it lands in
// a different place for a reason: a volume is a box tested AFTER the step, while
// this is consulted INSIDE the sweep by every leaf test. Which means the guard
// here is not "the launch fired once" but "the hull never touched it" -- and
// that has to hold at any split, since splitting the tick multiplies the number
// of sweeps a wall gets tested against.
//
// The floor is object 0 and the wall object 1, so the bitset is {0, 1}: INDEX,
// not uid, which is the whole point of keying it the way Collision_Id is keyed.
// Getting that wrong disables the floor instead, which is why the last check
// below is that the floor is still there.
static Bounding_Volume_Hierarchy floor_and_wall_world()
{
  const shared::geometry_value_t floor =
      shared::make_box_brush({0.f, -64.f, 0.f}, {2048.f, 64.f, 2048.f});
  const shared::geometry_value_t wall =
      shared::make_box_brush({128.f, 512.f, 0.f}, {16.f, 1024.f, 512.f});

  std::vector<BVH_Input> inputs;
  uint32_t               index = 0;
  for (const shared::geometry_value_t &geometry : {floor, wall})
  {
    for (const shared::collision_piece_t &piece : shared::get_collision_pieces(geometry, index + 1))
    {
      BVH_Input input;
      input.aabb             = piece.bounds;
      input.id               = {Collision_Id::Type::Static_Geometry, index};
      input.collision_planes = piece.planes;
      input.face_polygons    = piece.face_polygons;
      inputs.push_back(std::move(input));
    }
    ++index;
  }

  return build_bvh(inputs);
}

static void test_a_disabled_brush_is_walked_through(const cvar_state_t& cvars)
{
  printf("\n[EXACT] disabled geometry: the hull passes through, identically at any split\n");

  const Bounding_Volume_Hierarchy bvh = floor_and_wall_world();

  // COASTING, with no input and in the air: the only thing acting on x is
  // `x += v*dt`, which composes exactly, so any difference between splits is
  // the sweep and nothing else. A running player would bring the ground
  // acceleration's own split behaviour in with it, which is measured elsewhere
  // in this file and would drown this out.
  const vec3 start_position{0.f, 512.f, 0.f};
  const vec3 start_velocity{600.f, 0.f, 0.f};

  // Twenty ticks at 600 u/s is 200 units, which carries the hull from x = 0
  // clean past the wall: it spans [112, 144] and the hull is 16 wide, so the far
  // side is at x = 160.
  constexpr int ticks = 20;

  const uint8_t wall_disabled[] = {0, 1};

  float first_x = 0.f;
  for (int sub_steps : {1, 2, 8, 64})
  {
    entities::Movement  blocked_movement{};
    move_result_t       blocked{start_position, start_velocity};
    entities::Movement  open_movement{};
    move_result_t       open{start_position, start_velocity};

    for (int tick = 0; tick < ticks; ++tick)
    {
      blocked = run_split(cvars, bvh, Move_Input{}, blocked.position, blocked.velocity, tick_dt,
                          sub_steps, &blocked_movement);
      open    = run_split(cvars, bvh, Move_Input{}, open.position, open.velocity, tick_dt,
                          sub_steps, &open_movement, {}, nullptr, wall_disabled);
    }

    printf("    N=%-2d  solid x = %.6f   switched off x = %.6f\n", sub_steps, blocked.position.x,
           open.position.x);

    check(blocked.position.x < 100.f, "the solid wall stops the hull short of it");
    check(open.position.x > 160.f, "the switched-off one is passed straight through");

    if (sub_steps == 1)
      first_x = open.position.x;
    else
      check(open.position.x == first_x,
            "the run through it is BIT identical however many sub-steps each tick had");
  }

  // The floor is still there: the bitset names one object, not the world, and a
  // player falling through the ground is what getting the index wrong looks
  // like.
  {
    entities::Movement movement{};
    move_result_t      result{{0.f, 8.f, 0.f}, {0.f, 0.f, 0.f}};
    for (int tick = 0; tick < ticks; ++tick)
      result = run_split(cvars, bvh, Move_Input{}, result.position, result.velocity, tick_dt, 8,
                         &movement, {}, nullptr, wall_disabled);

    check(result.position.y >= -0.1f && result.position.y <= 0.1f,
          "disabling the wall leaves the floor solid");
  }
}

// The coyote clock is ACCUMULATED, so it has to sum to the same total whatever
// the split -- the plainest possible statement of what "step invariant" means
// for a piece of state rather than for a position.
static void test_time_since_grounded_composes(const cvar_state_t& cvars)
{
  printf("\n[EXACT] time_since_grounded: an accumulator composes\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  const Move_Input input;
  const vec3 start_position{0.f, 1000.f, 0.f};
  const vec3 start_velocity{0.f, 0.f, 0.f};

  for (int sub_steps : {1, 2, 8, 16})
  {
    entities::Movement movement{};
    (void)run_split(cvars, bvh, input, start_position, start_velocity, tick_dt, sub_steps,
                    &movement);

    printf("    N=%-2d  airborne = %.6f\n", sub_steps, movement.time_since_grounded_seconds);
    check_near(movement.time_since_grounded_seconds, tick_dt, 1e-5f,
               "airborne time sums to one tick however it was split");
    check(!movement.is_grounded, "a player in the void is not grounded");
  }
}

// The self-impulse cooldown is the second accumulator, counted DOWN, and it has
// to compose for the same reason the coyote clock does: a tick split eight ways
// must charge a dash exactly as much recovery as a tick that was not split.
//
// The impulse itself is applied by try_apply_self_impulse OUTSIDE player_move,
// which is what makes this the whole of player_move's obligation to it: a press
// opens a step, so the ability fires once per press by construction -- the same
// argument the air jump above makes, one layer up.
static void test_impulse_cooldown_composes(const cvar_state_t& cvars)
{
  printf("\n[EXACT] self-impulse cooldown: a countdown composes and clamps\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  const Move_Input input;
  const vec3 start_position{0.f, 1000.f, 0.f};
  const vec3 start_velocity{0.f, 0.f, 0.f};

  for (int sub_steps : {1, 2, 8, 16})
  {
    entities::Movement movement{};
    movement.seconds_until_impulse_ready = 1.f;
    (void)run_split(cvars, bvh, input, start_position, start_velocity, tick_dt, sub_steps,
                    &movement);

    printf("    N=%-2d  remaining = %.6f\n", sub_steps, movement.seconds_until_impulse_ready);
    check_near(movement.seconds_until_impulse_ready, 1.f - tick_dt, 1e-5f,
               "the cooldown spends one tick however that tick was split");
  }

  // A cooldown shorter than the tick it is spent in lands ON zero, never past
  // it: try_apply_self_impulse tests `> 0`, so a negative remainder would be
  // indistinguishable from ready and the clamp is what keeps that one value.
  entities::Movement nearly_ready{};
  nearly_ready.seconds_until_impulse_ready = tick_dt * 0.25f;
  (void)run_split(cvars, bvh, input, start_position, start_velocity, tick_dt, 8,
                  &nearly_ready);
  check(nearly_ready.seconds_until_impulse_ready == 0.f,
        "a cooldown that expires mid-tick clamps to exactly zero");
}

// --- 11. a ground jump flies its whole step: the arc starts at the impulse ---
static void test_ground_jump_arc_composes(const cvar_state_t& cvars)
{
  printf("\n[EXACT] ground jump: the parabola starts at the impulse\n");

  const Bounding_Volume_Hierarchy bvh = floor_world();
  Move_Input holding_jump;
  holding_jump.jump_pressed = true;

  const vec3 start_position{0.f, -0.02f, 0.f};
  const vec3 start_velocity{0.f, 0.f, 0.f};

  const float resting_height = -0.01f;
  const float expected_height = resting_height + cvars.pm_jumpspeed * tick_dt -
                                0.5f * cvars.g_gravity * tick_dt * tick_dt;
  const float expected_vertical_velocity = cvars.pm_jumpspeed - cvars.g_gravity * tick_dt;

  for (int sub_steps : {1, 2, 4, 16, 64})
  {
    const move_result_t result = run_split(cvars, bvh, holding_jump, start_position,
                                           start_velocity, tick_dt, sub_steps);
    printf("    N=%-2d  y = %.6f  vy = %.6f\n", sub_steps, result.position.y,
           result.velocity.y);
    check_near(result.position.y, expected_height, 2e-3f,
               "the jump tick ends on the exact parabola however it was split");
    check_near(result.velocity.y, expected_vertical_velocity, 2e-3f,
               "gravity runs from the impulse, not from the step after it");
  }
}

// --- 12. the clip is relative: carried speed survives flight, slide and hop -
static void test_carried_speed_survives_the_clip(const cvar_state_t& cvars)
{
  printf("\n[EXACT] carried speed: the clip never cuts what a step came in with\n");

  const Bounding_Volume_Hierarchy empty_bvh = empty_world();
  const Bounding_Volume_Hierarchy floor_bvh = floor_world();
  const Move_Input no_input;
  Move_Input holding_jump;
  holding_jump.jump_pressed = true;

  const vec3 airborne_position{0.f, 1000.f, 0.f};
  const vec3 grounded_position{0.f, -0.02f, 0.f};
  const vec3 carried_velocity{900.f, 0.f, 0.f};

  const float carried_speed = horizontal_speed(carried_velocity);
  const float slid_speed = carried_speed * std::exp(-cvars.pm_friction * tick_dt);

  for (int sub_steps : {1, 2, 8})
  {
    const move_result_t flying = run_split(cvars, empty_bvh, no_input, airborne_position,
                                           carried_velocity, tick_dt, sub_steps);
    const move_result_t sliding = run_split(cvars, floor_bvh, no_input, grounded_position,
                                            carried_velocity, tick_dt, sub_steps);
    const move_result_t hopping = run_split(cvars, floor_bvh, holding_jump, grounded_position,
                                            carried_velocity, tick_dt, sub_steps);

    printf("    N=%-2d  flying %.6f  sliding %.6f  hopping %.6f\n", sub_steps,
           horizontal_speed(flying.velocity), horizontal_speed(sliding.velocity),
           horizontal_speed(hopping.velocity));
    check_near(horizontal_speed(flying.velocity), carried_speed, 1e-2f,
               "free flight keeps speed above pm_maxspeed");
    check_near(horizontal_speed(sliding.velocity), slid_speed, 1e-2f,
               "on the ground only friction takes it");
    check_near(horizontal_speed(hopping.velocity), carried_speed, 1e-2f,
               "a hop keeps all of it");
  }
}

// --- 13. pm_bunnyhop cs: a sideways air push adds speed, and a fixed aim composes
static void test_bunnyhop_cs_strafe_gains(const cvar_state_t& cvars)
{
  printf("\n[EXACT] pm_bunnyhop cs: an air push adds speed where none only turns\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  Move_Input input;
  input.right_pressed = true;

  const vec3 start_position{0.f, 1000.f, 0.f};
  const vec3 start_velocity{cvars.pm_maxspeed, 0.f, 0.f};

  cvar_state_t strafing = cvars;
  strafing.pm_bunnyhop = cvars::Bunnyhop_Mode::cs;

  const float push = std::min(strafing.pm_air_acceleration * strafing.pm_maxspeed * tick_dt,
                              strafing.pm_air_speed_cap);
  const float expected_speed =
      std::sqrt(strafing.pm_maxspeed * strafing.pm_maxspeed + push * push);

  for (int sub_steps : {1, 2, 8})
  {
    const move_result_t gained = run_split(strafing, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    const move_result_t turned = run_split(cvars, bvh, input, start_position,
                                           start_velocity, tick_dt, sub_steps);
    printf("    N=%-2d  cs %.6f  none %.6f\n", sub_steps,
           horizontal_speed(gained.velocity), horizontal_speed(turned.velocity));
    check_near(horizontal_speed(gained.velocity), expected_speed, 1e-2f,
               "cs: the push lengthens the velocity by the same amount under any split");
    check_near(horizontal_speed(turned.velocity), cvars.pm_maxspeed, 1e-2f,
               "none: the same push only turns you");
  }
}

// --- 14. pm_bunnyhop hl2: a ground jump adds its boost once, up to the ceiling --
static void test_bunnyhop_hl2_jump_boost(const cvar_state_t& cvars)
{
  printf("\n[EXACT] pm_bunnyhop hl2: a ground jump adds pm_jump_boost once\n");

  const Bounding_Volume_Hierarchy bvh = floor_world();
  Move_Input input;
  input.forward_pressed = true;
  input.jump_pressed    = true;

  const vec3 grounded_position{0.f, -0.02f, 0.f};

  cvar_state_t boosting = cvars;
  boosting.pm_bunnyhop = cvars::Bunnyhop_Mode::hl2;

  const vec3 running_velocity{boosting.pm_maxspeed, 0.f, 0.f};
  const vec3 near_ceiling_velocity{
      boosting.pm_jump_boost_max_speed - 0.5f * boosting.pm_jump_boost, 0.f, 0.f};

  for (int sub_steps : {1, 2, 8})
  {
    const move_result_t boosted = run_split(boosting, bvh, input, grounded_position,
                                            running_velocity, tick_dt, sub_steps);
    const move_result_t capped = run_split(boosting, bvh, input, grounded_position,
                                           near_ceiling_velocity, tick_dt, sub_steps);
    const move_result_t plain = run_split(cvars, bvh, input, grounded_position,
                                          running_velocity, tick_dt, sub_steps);
    printf("    N=%-2d  hl2 %.6f  near the ceiling %.6f  none %.6f\n", sub_steps,
           horizontal_speed(boosted.velocity), horizontal_speed(capped.velocity),
           horizontal_speed(plain.velocity));
    check_near(horizontal_speed(boosted.velocity),
               boosting.pm_maxspeed + boosting.pm_jump_boost, 1e-2f,
               "hl2: a running jump adds pm_jump_boost exactly once");
    check_near(horizontal_speed(capped.velocity), boosting.pm_jump_boost_max_speed, 1e-2f,
               "hl2: the boost stops at pm_jump_boost_max_speed");
    check_near(horizontal_speed(plain.velocity), cvars.pm_maxspeed, 1e-2f,
               "none: the same jump adds nothing");
  }
}

// --- 15. the air push sweeps the aim: an edge on a steady turn adds nothing ---
static vec3 velocity_after_a_turning_tick(const cvar_state_t& cvars,
                                          const Bounding_Volume_Hierarchy& bvh,
                                          const Move_Input& input, float start_yaw,
                                          float yaw_turn, uint32_t edge_slot,
                                          bool sweep_the_aim)
{
  entities::Movement movement{};
  vec3 position{0.f, 1000.f, 0.f};
  vec3 velocity{cvars.pm_maxspeed, 0.f, 0.f};

  const std::vector<uint32_t> step_ends =
      edge_slot == 0 ? std::vector<uint32_t>{shared::SUBTICK_SLOT_COUNT}
                     : std::vector<uint32_t>{edge_slot, shared::SUBTICK_SLOT_COUNT};
  const float slot_fraction = 1.f / static_cast<float>(shared::SUBTICK_SLOT_COUNT);

  uint32_t step_start = 0;
  for (uint32_t step_end : step_ends)
  {
    const uint32_t slot_count   = step_end - step_start;
    const float    yaw_at_start = start_yaw + yaw_turn * static_cast<float>(step_start) * slot_fraction;
    const float    yaw_at_end   = start_yaw + yaw_turn * static_cast<float>(step_end) * slot_fraction;
    const float    yaw_radians  = linalg::to_radians(yaw_at_start);
    const vec3     front{std::cos(yaw_radians), 0.f, std::sin(yaw_radians)};
    const vec3     right{-std::sin(yaw_radians), 0.f, std::cos(yaw_radians)};
    const aim_sweep_t sweep =
        sweep_the_aim ? aim_sweep_t{.yaw_change_degrees = yaw_at_end - yaw_at_start,
                                    .push_count         = slot_count}
                      : aim_sweep_t{};
    const float step_dt = tick_dt * static_cast<float>(slot_count) * slot_fraction;

    std::tie(position, velocity) = player_move(cvars, input, movement, bvh, {}, position,
                                               velocity, front, right, sweep, half_width,
                                               half_height, step_dt);
    step_start = step_end;
  }
  return velocity;
}

static void test_air_push_ignores_edges_on_a_steady_turn(const cvar_state_t& cvars)
{
  printf("\n[EXACT] air push: an extra edge on a steady mouse turn adds nothing\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  Move_Input input;
  input.right_pressed = true;

  cvar_state_t strafing = cvars;
  strafing.pm_bunnyhop = cvars::Bunnyhop_Mode::cs;

  const float    start_yaw = -6.f;
  const float    yaw_turn  = 3.f;
  const uint32_t edge_slot = 24;

  const vec3 swept_whole =
      velocity_after_a_turning_tick(strafing, bvh, input, start_yaw, yaw_turn, 0, true);
  const vec3 swept_split =
      velocity_after_a_turning_tick(strafing, bvh, input, start_yaw, yaw_turn, edge_slot, true);
  const vec3 stepped_whole =
      velocity_after_a_turning_tick(strafing, bvh, input, start_yaw, yaw_turn, 0, false);
  const vec3 stepped_split =
      velocity_after_a_turning_tick(strafing, bvh, input, start_yaw, yaw_turn, edge_slot, false);

  printf("    swept aim:         whole tick %.6f  with an edge %.6f\n",
         horizontal_speed(swept_whole), horizontal_speed(swept_split));
  printf("    one aim per step:  whole tick %.6f  with an edge %.6f\n",
         horizontal_speed(stepped_whole), horizontal_speed(stepped_split));

  check_near(swept_split.x, swept_whole.x, 1e-3f, "swept aim: the edge leaves x alone");
  check_near(swept_split.z, swept_whole.z, 1e-3f, "swept aim: the edge leaves z alone");
  check(horizontal_speed(stepped_split) > horizontal_speed(stepped_whole) + 0.1f,
        "one aim per step: the same edge buys speed, which is what the sweep removes");
}

// --- 16. a mover carries its rider the same under any step count ---
//
// mover_def.md ss12: the push is the mover's, once per tick, before the steps,
// and the steps collide with the mover's end-of-tick pose. So a rider's tick is
// push-then-steps, and the steps must not care how many of them there are.
constexpr shared::entity_uid_t platform_uid = 77;
const vec3 platform_half_extents{128.f, 8.f, 128.f};
const vec3 platform_start{0.f, 100.f, 0.f};
const vec3 platform_velocity{120.f, 60.f, 0.f};

static vec3 platform_center_at(uint32_t tick)
{
  return platform_start + platform_velocity * (tick_dt * static_cast<float>(tick));
}

static shared::mover_t platform_at(uint32_t tick)
{
  shared::mover_t mover;
  mover.uid                         = platform_uid;
  mover.pose_at_tick_start.position = platform_center_at(tick - 1);
  mover.pose_at_tick_end.position   = platform_center_at(tick);
  mover.pieces = shared::get_collision_pieces(
      shared::make_box_brush(platform_center_at(tick), platform_half_extents), platform_uid);
  mover.swept_bounds = mover.pieces.front().bounds;
  for (const shared::collision_piece_t& piece : shared::get_collision_pieces(
           shared::make_box_brush(platform_center_at(tick - 1), platform_half_extents),
           platform_uid))
    mover.swept_bounds = shared::union_aabb(mover.swept_bounds, piece.bounds);
  return mover;
}

struct ride_result_t
{
  vec3                 feet;
  shared::entity_uid_t ground_mover_uid = shared::null_entity_uid;
  shared::entity_uid_t crushed_by       = shared::null_entity_uid;
  uint32_t             crushed_at_tick  = 0;
};

static ride_result_t ride_platform(const cvar_state_t& cvars, const Bounding_Volume_Hierarchy& bvh,
                                   uint32_t ticks, int sub_steps)
{
  entities::Movement movement{};
  vec3 feet = platform_center_at(0) + vec3{0.f, platform_half_extents.y - 0.02f, 0.f};
  vec3 velocity{};
  ride_result_t result;

  for (uint32_t tick = 1; tick <= ticks; ++tick)
  {
    const shared::mover_t movers[] = {platform_at(tick)};
    const shared::predicted_world_t world{.movers = movers};
    const mover_push_t push =
        push_player_by_movers(bvh, world, movement, feet, half_width, half_height);
    feet = push.feet;
    if (push.crushed_by != shared::null_entity_uid && result.crushed_by == shared::null_entity_uid)
    {
      result.crushed_by      = push.crushed_by;
      result.crushed_at_tick = tick;
    }

    const move_result_t moved = run_split(cvars, bvh, Move_Input{}, feet, velocity, tick_dt,
                                          sub_steps, &movement, {}, nullptr, {}, movers);
    feet     = moved.position;
    velocity = moved.velocity;
  }
  result.feet             = feet;
  result.ground_mover_uid = movement.ground_mover_uid;
  return result;
}

static void test_a_mover_carries_its_rider(const cvar_state_t& cvars)
{
  printf("\n[EXACT] mover: a rider arrives with the platform under 1 step and under 16\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  constexpr uint32_t ticks = 60;

  const ride_result_t one     = ride_platform(cvars, bvh, ticks, 1);
  const ride_result_t sixteen = ride_platform(cvars, bvh, ticks, 16);
  const vec3 top = platform_center_at(ticks) + vec3{0.f, platform_half_extents.y, 0.f};

  printf("    platform top (%.4f, %.4f, %.4f)\n", top.x, top.y, top.z);
  printf("    N=1  feet (%.4f, %.4f, %.4f)\n", one.feet.x, one.feet.y, one.feet.z);
  printf("    N=16 feet (%.4f, %.4f, %.4f)\n", sixteen.feet.x, sixteen.feet.y, sixteen.feet.z);

  check(one.feet.x == sixteen.feet.x && one.feet.y == sixteen.feet.y &&
            one.feet.z == sixteen.feet.z,
        "the rider's feet agree bit for bit under 1 step and 16");
  check_near(one.feet.x, top.x, 1e-2f, "carried along x with the platform");
  check_near(one.feet.y, top.y, 0.1f, "standing on the platform's top");
  check(one.ground_mover_uid == platform_uid, "Movement::ground_mover_uid names the platform");
  check(one.crushed_by == shared::null_entity_uid, "an open sky crushes nobody");
}

static void test_a_mover_crushes_against_a_ceiling(const cvar_state_t& cvars)
{
  printf("\n[DELIBERATE] mover: a platform rising into a ceiling crushes its rider\n");

  const float ceiling_bottom = platform_start.y + platform_half_extents.y + 2.f * half_height + 20.f;
  const shared::geometry_value_t ceiling = shared::make_box_brush(
      {0.f, ceiling_bottom + 64.f, 0.f}, {4096.f, 64.f, 4096.f});
  std::vector<BVH_Input> inputs;
  for (const shared::collision_piece_t& piece : shared::get_collision_pieces(ceiling, 1))
  {
    BVH_Input input;
    input.aabb             = piece.bounds;
    input.id               = {Collision_Id::Type::Static_Geometry, 0};
    input.collision_planes = piece.planes;
    input.face_polygons    = piece.face_polygons;
    inputs.push_back(std::move(input));
  }
  const Bounding_Volume_Hierarchy bvh = build_bvh(inputs);

  const ride_result_t ride = ride_platform(cvars, bvh, 60, 1);
  printf("    crushed at tick %u (the gap closes after ~20 ticks)\n", ride.crushed_at_tick);
  check(ride.crushed_by == platform_uid, "the platform is named as the crusher");
  check(ride.crushed_at_tick > 15 && ride.crushed_at_tick < 30,
        "not before the gap closes, and not long after");
}

// --- 17. pm_acceleration instant: the velocity IS the input, ground and air --
static cvar_state_t instant_cvars(const cvar_state_t& cvars)
{
  cvar_state_t instant = cvars;
  instant.pm_acceleration = cvars::Acceleration_Mode::instant;
  return instant;
}

static void test_instant_velocity_is_the_input(const cvar_state_t& cvars)
{
  printf("\n[EXACT] pm_acceleration instant: one step to top speed, one step to a stop\n");

  const cvar_state_t instant = instant_cvars(cvars);
  const Bounding_Volume_Hierarchy empty_bvh = empty_world();
  const Bounding_Volume_Hierarchy floor_bvh = floor_world();

  Move_Input forward;
  forward.forward_pressed = true;
  Move_Input right;
  right.right_pressed = true;
  const Move_Input no_input;

  const vec3 grounded_position{0.f, -0.02f, 0.f};
  const vec3 airborne_position{0.f, 1000.f, 0.f};
  const vec3 running{instant.pm_maxspeed, 0.f, 0.f};

  for (int sub_steps : {1, 2, 8, 64})
  {
    const move_result_t started = run_split(instant, floor_bvh, forward, grounded_position,
                                            {10.f, 0.f, 0.f}, tick_dt, sub_steps);
    const move_result_t turned_on_ground = run_split(instant, floor_bvh, right, grounded_position,
                                                     running, tick_dt, sub_steps);
    const move_result_t turned_in_air = run_split(instant, empty_bvh, right, airborne_position,
                                                  running, tick_dt, sub_steps);
    const move_result_t stopped_on_ground = run_split(instant, floor_bvh, no_input,
                                                      grounded_position, running, tick_dt, sub_steps);
    const move_result_t stopped_in_air = run_split(instant, empty_bvh, no_input,
                                                   airborne_position, running, tick_dt, sub_steps);

    printf("    N=%-2d  started %.6f  turned ground (%.3f, %.3f)  air (%.3f, %.3f)  stopped %.6f / %.6f\n",
           sub_steps, horizontal_speed(started.velocity), turned_on_ground.velocity.x,
           turned_on_ground.velocity.z, turned_in_air.velocity.x, turned_in_air.velocity.z,
           horizontal_speed(stopped_on_ground.velocity), horizontal_speed(stopped_in_air.velocity));

    check_near(started.velocity.x, instant.pm_maxspeed, 1e-3f,
               "a press from nearly standing still is top speed within the tick");
    check_near(turned_on_ground.velocity.x, 0.f, 1e-3f, "ground: turning keeps nothing of the old direction");
    check_near(turned_on_ground.velocity.z, instant.pm_maxspeed, 1e-3f,
               "ground: turning is top speed along the new one");
    check_near(turned_in_air.velocity.x, 0.f, 1e-3f, "air: steering is the same as on the ground");
    check_near(turned_in_air.velocity.z, instant.pm_maxspeed, 1e-3f,
               "air: top speed along the new direction");
    check_near(horizontal_speed(stopped_on_ground.velocity), 0.f, 1e-3f,
               "ground: no input is no velocity");
    check_near(horizontal_speed(stopped_in_air.velocity), 0.f, 1e-3f,
               "air: no input is no velocity, there is no air momentum");
  }
}

// --- 18. borrowed speed keeps its size while the input steers it ------------
static void test_instant_borrowed_speed_is_steered(const cvar_state_t& cvars)
{
  printf("\n[EXACT] pm_acceleration instant: borrowed speed is steered, then returned\n");

  const cvar_state_t instant = instant_cvars(cvars);
  const Bounding_Volume_Hierarchy bvh = empty_world();

  Move_Input right;
  right.right_pressed = true;
  const Move_Input no_input;

  const vec3 airborne_position{0.f, 1000.f, 0.f};
  const vec3 dashing{900.f, 0.f, 0.f};

  for (int sub_steps : {1, 2, 8, 64})
  {
    entities::Movement steered_movement{};
    steered_movement.seconds_until_speed_returns_to_base_speed = 1.f;
    const move_result_t steered = run_split(instant, bvh, right, airborne_position, dashing,
                                            tick_dt, sub_steps, &steered_movement);

    entities::Movement coasting_movement{};
    coasting_movement.seconds_until_speed_returns_to_base_speed = 1.f;
    const move_result_t coasting = run_split(instant, bvh, no_input, airborne_position, dashing,
                                             tick_dt, sub_steps, &coasting_movement);

    printf("    N=%-2d  steered (%.3f, %.3f)  coasting %.6f  remaining %.6f\n", sub_steps,
           steered.velocity.x, steered.velocity.z, coasting.velocity.x,
           steered_movement.seconds_until_speed_returns_to_base_speed);

    check_near(steered.velocity.x, 0.f, 1e-3f, "the input picks the direction instantly");
    check_near(steered.velocity.z, 900.f, 1e-3f, "and the borrowed speed keeps its size");
    check_near(coasting.velocity.x, 900.f, 1e-3f, "no input keeps the borrowed velocity as it was");
    check_near(steered_movement.seconds_until_speed_returns_to_base_speed, 1.f - tick_dt, 1e-5f,
               "the timer spends one tick however that tick was split");
  }

  entities::Movement expiring{};
  expiring.seconds_until_speed_returns_to_base_speed = 0.1f;
  move_result_t result{airborne_position, dashing};
  for (int tick = 0; tick < 10; ++tick)
    result = run_split(instant, bvh, no_input, result.position, result.velocity, tick_dt, 4,
                       &expiring);
  check(expiring.seconds_until_speed_returns_to_base_speed == 0.f, "the timer clamps at zero");
  check_near(horizontal_speed(result.velocity), 0.f, 1e-3f,
             "once it runs out, no input is no velocity again");
}

// --- 19. a pad launch borrows its speed for the whole flight ----------------
static void test_instant_pad_launch_is_borrowed(const cvar_state_t& cvars)
{
  printf("\n[EXACT] pm_acceleration instant: a pad's arc survives an idle input\n");

  const cvar_state_t instant = instant_cvars(cvars);
  const Bounding_Volume_Hierarchy bvh = floor_world();
  const vec3 launch{300.f, 900.f, 0.f};
  const std::vector<shared::movement_volume_t> volumes = {
      pad_at({0.f, 8.f, 0.f}, {32.f, 8.f, 32.f}, launch, true)};
  const float flight_seconds = 2.f * launch.y / instant.g_gravity;

  for (int sub_steps : {1, 2, 8})
  {
    entities::Movement movement{};
    pad_probe_t        pad{};
    move_result_t result = run_split(instant, bvh, Move_Input{}, {0.f, 0.f, 0.f},
                                     {0.f, 0.f, 0.f}, tick_dt, sub_steps, &movement,
                                     Span<const shared::movement_volume_t>(volumes), &pad);
    const float remaining_after_launch = movement.seconds_until_speed_returns_to_base_speed;

    for (int tick = 0; tick < 10; ++tick)
      result = run_split(instant, bvh, Move_Input{}, result.position, result.velocity, tick_dt,
                         sub_steps, &movement);

    printf("    N=%-2d  launches %d  remaining %.6f  x speed ten ticks later %.6f\n", sub_steps,
           pad.launches, remaining_after_launch, result.velocity.x);

    check(pad.launches == 1, "the pad fires once");
    check(remaining_after_launch > flight_seconds - tick_dt &&
              remaining_after_launch <= flight_seconds,
          "the launch borrows its speed for the time it takes to fall back to launch height");
    check_near(result.velocity.x, launch.x, 1e-3f,
               "the horizontal half of the launch survives an idle input");
  }
}

// --- the hook's reel ---------------------------------------------------------
//
// The reel is a branch that OVERWRITES velocity every step, so nothing about it
// composes the way an acceleration does -- what has to compose is where it
// stops. Both caps in the branch exist for this test: without the distance one
// the stop position lands anywhere inside the arrival sphere depending on how
// the tick was cut, and without the time one the last step overshoots by up to
// a whole step's travel.
static void test_hook_reel_arrival_is_step_invariant(const cvar_state_t& cvars)
{
  printf("\n[EXACT] hook reel: the arrival point does not move with the step count\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  const vec3 anchor{0.f, half_height, 600.f};

  for (int sub_steps : {1, 2, 8})
  {
    entities::Movement movement{};
    movement.hook_anchor_position           = anchor;
    movement.seconds_of_hook_pull_remaining = cvars.pm_hook_max_pull_seconds;

    hook_probe_t  hook{};
    move_result_t result{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
    for (int tick = 0; tick < 120 && hook.releases == 0; ++tick)
      result = run_split(cvars, bvh, Move_Input{}, result.position, result.velocity, tick_dt,
                         sub_steps, &movement, {}, nullptr, {}, {}, &hook);

    const float distance_at_release = length(anchor - hook.release_position);

    printf("    N=%-2d  releases %d  distance %.6f  speed %.6f\n", sub_steps, hook.releases,
           distance_at_release, length(hook.release_velocity));

    check(hook.releases == 1, "the reel lets go exactly once");
    check_near(distance_at_release, cvars.pm_hook_arrive_radius, 1e-2f,
               "the reel lets go on the arrival sphere, not wherever a step landed");
    check_near(hook.release_position.y, half_height, 1e-2f,
               "a reel straight along z never falls: it applies no gravity");
    check_near(length(hook.release_velocity), cvars.pm_hook_reel_speed, 1e-2f,
               "it lets go at full reel speed, not at the last step's leftover");
    check(movement.hook_anchor_uid == shared::null_entity_uid,
          "arriving clears the tether");
    check(movement.seconds_until_speed_returns_to_base_speed > 0.f,
          "the release borrows its speed, or instant erases it next step");
  }
}

// A pull that runs out of TIME before it runs out of distance travels
// reel_speed * duration, whatever the step count -- which is what the reel_dt
// clamp buys. Without it the final step spends a whole dt on a pull with only a
// sliver left.
static void test_hook_reel_timeout_is_step_invariant(const cvar_state_t& cvars)
{
  printf("\n[EXACT] hook reel: a pull that times out travels the same distance\n");

  const Bounding_Volume_Hierarchy bvh = empty_world();
  const float pull_seconds = 0.25f;
  // Far enough that the timer, not the arrival radius, is what ends it.
  const vec3  anchor{0.f, half_height, 100000.f};
  const float expected_travel = cvars.pm_hook_reel_speed * pull_seconds;

  for (int sub_steps : {1, 2, 8})
  {
    entities::Movement movement{};
    movement.hook_anchor_position           = anchor;
    movement.seconds_of_hook_pull_remaining = pull_seconds;

    hook_probe_t  hook{};
    move_result_t result{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}};
    for (int tick = 0; tick < 120 && hook.releases == 0; ++tick)
      result = run_split(cvars, bvh, Move_Input{}, result.position, result.velocity, tick_dt,
                         sub_steps, &movement, {}, nullptr, {}, {}, &hook);

    printf("    N=%-2d  releases %d  travelled %.6f  expected %.6f\n", sub_steps, hook.releases,
           hook.release_position.z, expected_travel);

    check(hook.releases == 1, "the reel lets go exactly once");
    check_near(hook.release_position.z, expected_travel, 1e-2f,
               "a timed-out pull travels reel_speed * duration however the tick was cut");
    check(movement.hook_anchor_uid == shared::null_entity_uid,
          "timing out clears the tether");
  }
}

int main()
{
  printf("player_move_step_invariance_test\n");
  printf("  dt = %.6f (60Hz), g_gravity = 800, pm_friction = 6\n", tick_dt);
  printf("  gravity position scheme: %s\n",
         gravity_position_uses_endpoint_average
             ? "endpoint average (trapezoid)"
             : "end velocity (semi-implicit Euler)");

  const cvar_state_t cvars;

  test_gravity_velocity_is_exact(cvars);
  test_gravity_position_is_first_order(cvars);
  test_air_horizontal_composes(cvars);
  test_friction_speed_composes(cvars);
  test_friction_floor_composes(cvars);
  test_ground_position_is_first_order(cvars);
  test_ground_accelerate_composes(cvars);
  test_ground_saturation_is_step_invariant(cvars);
  test_maxspeed_clip_diverges(cvars);
  test_accelerate_clamp_alone_composes(cvars);
  test_air_jump_fires_once_per_press(cvars);
  test_time_since_grounded_composes(cvars);
  test_impulse_cooldown_composes(cvars);
  test_ground_jump_arc_composes(cvars);
  test_carried_speed_survives_the_clip(cvars);
  test_bunnyhop_cs_strafe_gains(cvars);
  test_bunnyhop_hl2_jump_boost(cvars);
  test_air_push_ignores_edges_on_a_steady_turn(cvars);
  test_a_jump_pad_fires_once_per_contact(cvars);
  test_a_disabled_jump_pad_is_passed_through(cvars);
  test_a_disabled_brush_is_walked_through(cvars);
  test_a_mover_carries_its_rider(cvars);
  test_a_mover_crushes_against_a_ceiling(cvars);
  test_instant_velocity_is_the_input(cvars);
  test_instant_borrowed_speed_is_steered(cvars);
  test_instant_pad_launch_is_borrowed(cvars);
  test_hook_reel_arrival_is_step_invariant(cvars);
  test_hook_reel_timeout_is_step_invariant(cvars);

  printf(failures == 0 ? "\nplayer_move_step_invariance_test PASSED\n"
                       : "\nplayer_move_step_invariance_test FAILED (%d)\n",
         failures);
  return failures == 0 ? 0 : 1;
}
