// The pin for a bubble's life as a function of the tick: its position never written, its pop and reap and the
// swell and peel the client draws all read off the launch and the pop stamp.
#include "assets/asset_state.hpp"
#include "entities/generated/entities/bubble_entity_generated.hpp"
#include "movement/bubble.hpp"
#include "tick_clock.hpp"

#include <cmath>
#include <cstdio>

using linalg::vec3f;

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

static bool near(float a, float b, float tolerance = 1e-3f)
{
  return std::fabs(a - b) <= tolerance;
}

constexpr uint32_t TICKRATE = 100;

static entities::Bubble_Entity launched_bubble()
{
  entities::Bubble_Entity bubble{};
  bubble.position            = {0.f, 300.f, 0.f};
  bubble.flight              = {.launch_position = {0.f, 300.f, 0.f}, .launch_tick = 10, .flight_ticks = 50};
  bubble.projectile.velocity = {600.f, 0.f, 0.f};
  bubble.rest_seconds        = 2.f;
  bubble.swell_seconds       = 0.1f;
  bubble.swell_scale         = 1.5f;
  bubble.peel_seconds        = 0.2f;
  bubble.linger_seconds      = 0.35f;
  return bubble;
}

int main()
{
  // A Bubble_Entity's Render default names a mesh, which resolves through the asset state.
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  const shared::fixed_arc_flight_settings_t flight{.tickrate_hz = TICKRATE, .gravity = -800.f};

  printf("[pin] seconds_since_tick is signed and carries the fraction\n");
  {
    check(near(shared::seconds_since_tick(100, 150, 0.f, TICKRATE), 0.5f), "50 ticks at 100 Hz is half a second");
    check(near(shared::seconds_since_tick(100, 150, 0.5f, TICKRATE), 0.505f), "the fraction is part of a tick");
    check(near(shared::seconds_since_tick(150, 100, 0.f, TICKRATE), -0.5f), "negative while the anchor is ahead");
  }

  printf("\n[pin] the position is the flight's, and the spawn point only before the launch is latched\n");
  {
    entities::Bubble_Entity bubble = launched_bubble();
    bubble.flight.launch_tick      = 0;
    check(shared::bubble_position_at_tick(bubble, 5, flight) == bubble.position, "unlaunched: where it was spawned");
    check(shared::bubble_timed_pop_tick(bubble, TICKRATE) == 0, "unlaunched: no timed pop yet");

    bubble = launched_bubble();
    check(shared::bubble_position_at_tick(bubble, 10, flight) == bubble.flight.launch_position,
          "the launch tick is the launch position");
    const vec3f at_30   = shared::bubble_position_at_tick(bubble, 30, flight);
    const vec3f at_31   = shared::bubble_position_at_tick(bubble, 31, flight);
    const vec3f between = shared::bubble_position_at_tick(bubble, 30, 0.5f, flight);
    check(at_30.x > bubble.flight.launch_position.x, "it has travelled by tick 30");
    check(near(between.x, 0.5f * (at_30.x + at_31.x)), "half a tick on is halfway to the next tick");
    check(shared::bubble_position_at_tick(bubble, 60, flight) == shared::bubble_position_at_tick(bubble, 1000, flight),
          "at rest after it lands");
  }

  printf("\n[pin] a whole bubble pops when its rest runs out, and the swell runs up to that tick\n");
  {
    const entities::Bubble_Entity bubble   = launched_bubble();
    const uint32_t                pop_tick = shared::bubble_timed_pop_tick(bubble, TICKRATE);
    check(pop_tick == 10 + 50 + 200, "landed plus two seconds of rest");

    const shared::bubble_state_t early = shared::bubble_state_at_tick(bubble, 100, 0.f, TICKRATE);
    check(!early.has_burst && early.swell == 0.f, "whole and unswollen long before the pop");

    const shared::bubble_state_t swelling = shared::bubble_state_at_tick(bubble, pop_tick - 5, 0.f, TICKRATE);
    check(!swelling.has_burst && near(swelling.swell, 0.5f), "half swollen five ticks ahead of a 0.1 s swell");

    const shared::bubble_state_t at_pop = shared::bubble_state_at_tick(bubble, pop_tick, 0.f, TICKRATE);
    check(!at_pop.has_burst && at_pop.swell == 1.f, "fully swollen at the pop tick; the burst needs the stamp");
    check(!shared::bubble_has_expired_at_tick(bubble, pop_tick + 1000, TICKRATE), "never reaped while whole");
  }

  printf("\n[pin] a timed pop bursts on its stamp and peels from there; a bounce pop bursts on notice\n");
  {
    entities::Bubble_Entity bubble = launched_bubble();
    bubble.popped_tick             = shared::bubble_timed_pop_tick(bubble, TICKRATE);
    bubble.popped_by               = shared::null_entity_uid;

    const shared::bubble_state_t at_stamp = shared::bubble_state_at_tick(bubble, bubble.popped_tick, 0.f, TICKRATE);
    check(at_stamp.has_burst && at_stamp.peeled == 0.f && !at_stamp.has_vanished, "burst on the stamp, nothing peeled yet");

    const shared::bubble_state_t half = shared::bubble_state_at_tick(bubble, bubble.popped_tick + 10, 0.f, TICKRATE);
    check(near(half.peeled, 0.5f) && !half.has_vanished, "half peeled 0.1 s into a 0.2 s peel");

    const shared::bubble_state_t through = shared::bubble_state_at_tick(bubble, bubble.popped_tick + 20, 0.f, TICKRATE);
    check(through.peeled >= 0.999f, "the peel is through 0.2 s on");
    const shared::bubble_state_t gone = shared::bubble_state_at_tick(bubble, bubble.popped_tick + 21, 0.f, TICKRATE);
    check(gone.has_vanished && gone.peeled == 1.f, "and gone the tick after");

    check(!shared::bubble_has_expired_at_tick(bubble, bubble.popped_tick + 34, TICKRATE) &&
              shared::bubble_has_expired_at_tick(bubble, bubble.popped_tick + 35, TICKRATE),
          "reaped linger_seconds after the stamp");

    bubble.popped_tick = 100;
    bubble.popped_by   = 7;
    const shared::bubble_state_t on_notice = shared::bubble_state_at_tick(bubble, 100, 0.f, TICKRATE);
    check(!on_notice.has_burst && on_notice.swell == 0.f, "a bounce pop is whole on its stamp");
    const shared::bubble_state_t swollen = shared::bubble_state_at_tick(bubble, 110, 0.f, TICKRATE);
    check(swollen.swell >= 0.999f, "fully swollen swell_seconds later");
    const shared::bubble_state_t burst = shared::bubble_state_at_tick(bubble, 111, 0.f, TICKRATE);
    check(burst.has_burst && burst.peeled < 0.1f, "and burst the tick after, the peel just begun");
  }

  printf("\n%s\n", failure_count == 0 ? "ALL PASSED" : "FAILURES");
  return failure_count == 0 ? 0 : 1;
}
