// The pin for a fizzling weapon's clock: the server reaps and the client dissolves off the same spawn stamp.
#include "assets/asset_state.hpp"
#include "entities/generated/entities/emancipated_weapon_entity_generated.hpp"
#include "emancipated_weapon.hpp"

#include <cmath>
#include <cstdio>

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

int main()
{
  // Entity defaults resolve mesh and material names through the asset state.
  static assets::asset_state_t asset_state;
  assets::set_state(&asset_state);
  assets::number_asset_ids_from_tree(asset_state, "resources");

  entities::Emancipated_Weapon_Entity fizzled{};
  fizzled.spawned_tick     = 1000;
  fizzled.lifetime_seconds = 2.f;

  printf("[pin] gone runs 0 to 1 over the lifetime and the reap lands on its last tick\n");
  {
    const shared::emancipated_weapon_state_t fresh = shared::emancipated_weapon_state_at_tick(fizzled, 1000, 0.f, TICKRATE);
    check(fresh.gone == 0.f && fresh.elapsed_seconds == 0.f && !fresh.has_vanished, "fresh on its spawn tick");

    const shared::emancipated_weapon_state_t half = shared::emancipated_weapon_state_at_tick(fizzled, 1100, 0.f, TICKRATE);
    check(near(half.gone, 0.5f) && near(half.elapsed_seconds, 1.f), "half gone a second in");

    const shared::emancipated_weapon_state_t between = shared::emancipated_weapon_state_at_tick(fizzled, 1100, 0.5f, TICKRATE);
    check(near(between.elapsed_seconds, 1.005f), "the draw's fraction counts");

    check(!shared::emancipated_weapon_has_vanished_at_tick(fizzled, 1199, TICKRATE) &&
              shared::emancipated_weapon_has_vanished_at_tick(fizzled, 1200, TICKRATE),
          "reaped exactly lifetime_seconds of ticks later");

    const shared::emancipated_weapon_state_t late = shared::emancipated_weapon_state_at_tick(fizzled, 1500, 0.f, TICKRATE);
    check(late.gone == 1.f && late.has_vanished, "clamped and gone past the lifetime");
  }

  printf("\n[pin] a lifetime of zero is gone at once and never divides by it\n");
  {
    fizzled.lifetime_seconds = 0.f;
    const shared::emancipated_weapon_state_t state = shared::emancipated_weapon_state_at_tick(fizzled, 1000, 0.f, TICKRATE);
    check(state.gone == 1.f && state.has_vanished, "gone on its own spawn tick");
  }

  printf("\n[pin] a snapshot from before the spawn reads as fresh, not as negative age\n");
  {
    fizzled.lifetime_seconds = 2.f;
    const shared::emancipated_weapon_state_t state = shared::emancipated_weapon_state_at_tick(fizzled, 990, 0.f, TICKRATE);
    check(state.elapsed_seconds == 0.f && state.gone == 0.f && !state.has_vanished, "clamped at zero");
  }

  printf("\n%s\n", failure_count == 0 ? "ALL PASSED" : "FAILURES");
  return failure_count == 0 ? 0 : 1;
}
