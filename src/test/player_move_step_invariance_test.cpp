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
  shared::box_geometry_t box;
  box.position = vec3{0.f, -64.f, 0.f};
  box.half_extents = vec3{2048.f, 64.f, 2048.f};

  const shared::geometry_value_t geometry = box;

  BVH_Input input;
  input.aabb = shared::get_bounds(geometry);
  input.id = {Collision_Id::Type::Static_Geometry, 0};
  input.collision_planes = shared::get_collision_planes(geometry);
  input.face_polygons = shared::get_face_polygons(geometry);

  return build_bvh({input});
}

struct move_result_t
{
  vec3 position;
  vec3 velocity;
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
                               entities::Movement* movement = nullptr)
{
  entities::Movement local_movement{};
  entities::Movement& state = movement != nullptr ? *movement : local_movement;

  const float step_dt = total_dt / (float)sub_steps;
  for (int i = 0; i < sub_steps; ++i)
  {
    std::tie(position, velocity) =
        player_move(cvars, input, state, bvh, position, velocity, look_front,
                    look_right, half_width, half_height, step_dt);
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
  const vec3 start_position{0.f, half_height - 0.02f, 0.f};
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
  const vec3 start_position{0.f, half_height - 0.02f, 0.f};
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
  const vec3 start_position{0.f, half_height - 0.02f, 0.f};
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

  const vec3 start_position{0.f, half_height - 0.02f, 0.f};
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

  const vec3 start_position{0.f, half_height - 0.02f, 0.f};
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
// in step_air_move: above pm_maxspeed it normalizes the horizontal vector and
// rescales, so the DIRECTION going into the next accelerate depends on when the
// clip fired. Split finer and the lateral gain is preserved rather than being
// renormalized away in one lump -- which is bunnyhopping, arriving through the
// clip rather than through the clamp.
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
    check_near(horizontal_speed(result.velocity), cvars.pm_maxspeed, 1e-2f,
               "the clip pins horizontal speed to pm_maxspeed either way");
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

  printf(failures == 0 ? "\nplayer_move_step_invariance_test PASSED\n"
                       : "\nplayer_move_step_invariance_test FAILED (%d)\n",
         failures);
  return failures == 0 ? 0 : 1;
}
