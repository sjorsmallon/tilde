# Hitscan Spine: weapon table, resolve_hitscan, player hitboxes

Steps A–C of the scoutzknivez track: fixed two-weapon loadout (scout +
knife), hitscan resolution with headshots, damage through the existing
`inflict_damage` choke point. Playable against bots with capsule
rendering.

## Non-goals (deliberate, see events_plan.md conventions)

- **Lag compensation.** `resolve_hitscan` takes candidate positions as
  parameters so rewind later changes the caller, not the function.
  Skip until a second human plays over real latency.
- **Skeletal animation / bone-attached hitboxes.** Hitboxes are a
  static table offset from the player's position — not yaw-rotated
  either, see §2.
- **Viewmodel, zoom, tracers, decals.** `BULLET_IMPACT` handler is a
  log stub like the other cosmetic handlers.
- **Weapon pickups / drops.** No Weapon_Entity spawns; the loadout is
  fixed. Entities re-enter when a pickup mode exists.

## 1. Weapon definition table — `src/shared/weapons.hpp` — **LANDED**

Static table in shared code, same pattern as the reflection tables:
both sides compile it, nothing rides the wire.

**Two enums, not one.** The draft had a single `weapon_kind_t` doubling
as identity and wire id; splitting it is better, because the fire path
wants to switch on *how a shot resolves* while the wire wants *which
weapon it was*, and those are not the same axis (Knife and Scout are
both hitscan; a second sniper later would share Scout's resolution but
need its own id).

```cpp
enum class weapon_id_t   : uint16_t { Knife = 0, Scout = 1, Rocket_Launcher = 2 };
enum class weapon_kind_t : uint16_t { Melee, Hitscan, Projectile, Sniper };

struct weapon_definition_t
{
  const char*   display_name;
  float         damage;
  float         headshot_multiplier;   // Knife / Rocket: 1.0, no headshots
  float         fire_interval_seconds;
  float         range;                 // knife 50, scout map-length
  weapon_kind_t kind;
};

inline constexpr std::array<weapon_definition_t, weapon_count> WEAPON_DEFINITIONS{...};
constexpr const weapon_definition_t& get_weapon_definition(weapon_id_t id);
```

`uint16_t(weapon_id_t)` **is** the `weapon_id` in `damage_info_t` /
`player_died_payload_t` / `rocket_detonated_payload_t` — resolves the
"placeholder until an allocation scheme exists" note in events_plan.md.
Rocket_Launcher has a value so the existing projectile path can stamp
one too. The table is indexed by `weapon_id_t`, so its order must
track that enum, not `weapon_kind_t`.

## 2. Player hitbox table — `src/shared/player_hitboxes.hpp` — **LANDED**

Three regions, identical for every player, so a static table — NOT
schema fields. `Player_Entity` already carries `hitbox: Hitbox`, a
single `@Networked` capsule for rocket splash and overlap tests; that
one stays and varies per entity. The three regions are different: they
are a compile-time constant, so per-instance schema fields would be 3×
duplication, and `Hitbox`'s `@Networked` reason (rockets are
runtime-spawned, the client has no other source) does not apply —
hit decisions are server-side and a debug overlay compiles the same
table. Lag compensation is the tiebreaker: keeping them out of the
entity means `Snapshot_History` never has to carry them, and the
rewind surface stays `position` alone.

```cpp
enum class hit_region_t : uint16_t { Head = 0, Torso = 1, Legs = 2 };

struct player_hitbox_t
{
  hit_region_t         region;
  entities::Shape_Kind shape;   // Head = Sphere, Torso/Legs = Box
  vec3f                offset;  // from the player's FEET, straight up
  vec3f                size;    // half-extents; Sphere uses .x as the
                                // radius, matching entities::Hitbox
};

inline constexpr std::array<player_hitbox_t, 3> player_hitboxes{ ... };
```

**Yaw is not applied, and there is no posing function.** An earlier
draft had `player_hitboxes_world(position, yaw) → posed_hitbox_t[3]`
and ray-vs-oriented-box. Both are unnecessary: the head is a sphere on
the player's vertical axis, where rotation is exactly a no-op, and the
torso and legs are square columns on that same axis, where it is very
nearly one. So posing is `target.position + hitbox.offset` inline, and
every test is axis-aligned against the `intersect_ray_aabb` /
`intersect_ray_sphere` already in `linalg.hpp`. Pitch is ignored for
the same reason it always was (the head stays atop the capsule). If
yaw ever earns its keep, the cheap way in is transforming the *ray*
into player-local space, not the boxes into world space.

Sizes derive from the combat capsule at `server_impl.cpp:428` — radius
18, half-height 38, origin at the feet, so ~76 tall and 36 wide. The
regions tile it: legs 0..30, torso 30..56, head sphere 56..76. Tune by
eye with a debug overlay later.

## 3. `resolve_hitscan` — `src/shared/hitscan.{hpp,cpp}` — **LANDED**

Pure simulation per the events_plan convention: data in, data out, no
event firing, no world mutation.

```cpp
struct hitscan_target_t     // caller builds this list — lag comp later
{                           // means feeding rewound positions here
  entity_uid_t uid;
  vec3f        position;    // the player's FEET
};

struct hitscan_result_t
{
  entity_uid_t hit_uid = 0;                    // 0 = nothing hit
  hit_region_t region  = hit_region_t::Torso;  // valid when hit_uid != 0
  vec3f        impact_point{};
  vec3f        impact_normal{};
  float        distance = 0.f;
};

// PRECONDITION: `direction` is normalized.
hitscan_result_t resolve_hitscan(vec3f origin, vec3f direction,
                                 float max_range,
                                 Span<const hitscan_target_t> targets);
```

**The world cast lives in the CALLER, not in here.** An earlier draft
took a `physics_state_t&` and did both casts internally. Splitting them
is strictly better: the server already holds `g_state.physics`, and
passing the wall distance as `max_range` makes "a wall blocks the shot"
a clamp rather than a second comparison. What is left is a pure
function with no Jolt headers, and §7's wall case needs no
`physics_state_t` — it is just a shorter `max_range` down the identical
code path. `hit_world` left the result struct for the same reason: the
caller knows whether its own cast hit.

So, two casts at the call site, closest wins:
- **World**: new `cast_ray_static` in physics.{hpp,cpp} beside
  `cast_sphere_static` — same STATIC-layer filter, Jolt RayCast
  instead of a shape sweep. **Not written yet** — this is the one
  piece of §3 still outstanding.
- **Players**: `resolve_hitscan`. Analytic ray-vs-sphere /
  ray-vs-AABB against `player_hitboxes` offset from each target's
  position, closest distance across *all* targets and *all* regions
  wins. Negative distances are rejected, so a muzzle already
  overlapping a player misses rather than reporting an impact behind
  the shooter. No Jolt involvement.

**Corpse policy, decided:** the server builds `targets` from
*alive* players only, and the world cast is static-only — so corpses
are invisible to hitscan by construction while their Jolt capsules
keep blocking movement. Closes the "corpses: air or walls?" question
in events_plan.md for the hitscan case without touching physics
registration.

## 4. Weapon switching — server_impl.cpp

`Button::Key1` / `Key2` already exist in the bitfield and
`Player_Entity::active_weapon_id` is already `@Networked`. Server maps
Key1→Knife, Key2→Scout on press (edge-detected like Fire is today),
writes `active_weapon_id`. Client reads its own entity's field for HUD
text. No proto change, no schema change.

## 5. Server fire path — server_impl.cpp

The `fire_pressed` block (server_impl.cpp:866) becomes a switch on
`active_weapon_id`:

- **Rocket**: existing block, unchanged (rockets stay a third weapon
  or move behind Key3 — keep them, they're the regression test).
- **Scout / Knife**: gate on `fire_interval_seconds` via a
  `last_fire_tick` in the per-player state (`g_player_states`), build
  the eye position and view direction (reuse the yaw/pitch→dir math
  already in the rocket block), then the two casts from §3:

  ```cpp
  hit_result_t world_hit{};
  float range = weapon.range;
  if (cast_ray_static(*g_state.physics, eye, eye + direction * range, world_hit))
    range = world_hit.fraction * weapon.range;   // clamp to the wall

  const hitscan_result_t hit = resolve_hitscan(eye, direction, range, targets);
  ```

  - `hit.hit_uid != 0` → `inflict_damage` with
    `amount = damage * (hit.region == Head ? headshot_multiplier : 1)`,
    `was_headshot`, `weapon_id`, `attacker_uid = inflictor_uid`.
  - otherwise, if the world cast hit → dispatch `BULLET_IMPACT`
    cosmetic effect at `world_hit.position` / `.normal` (knife skips
    this for now).
  - otherwise → nothing was in range.

  `targets` is every *alive* player except the shooter, per the corpse
  policy above.

`damage_info_t` already carries every field needed — no change to
damage.{hpp,cpp} beyond §6.

## 6. PLAYER_DAMAGED wiring

Declared in game_events.hpp but dead: no serialize case, no dispatch,
no consumer. Wire it end-to-end per src/shared/EVENTS.md:
serialize/deserialize case, fired from `inflict_damage` on every
nonlethal application (lethal keeps firing PLAYER_DIED only — decide:
both, or died-implies-damaged? **Fire both**; consumers like a damage
number popup shouldn't special-case the killing blow), plus a
log-stub consumer in the client game_events switch (future hitmarker /
damage indicator mounts there). Benefits rockets too, free.

## 7. Test — `src/test/hitscan_test.cpp`

Standalone executable, added to `GAME_TESTS`. Pure-function coverage —
no server, and (since §3's split) no `physics_state_t` either:

- ray through head sphere → `region == Head`
- ray through torso from behind → Torso
- ray through legs → Legs, i.e. a lower region is not masked by an
  earlier table entry — the guard against "first hit in table order
  wins" instead of nearest
- two targets in line → nearer uid wins
- wall between origin and target → a `max_range` shorter than the
  target distance, so `hit_uid == 0`. The caller's `cast_ray_static`
  is what produces that clamp in the real path; the test supplies it
  directly.
- target beyond `max_range` → miss
- origin inside a hitbox → miss, not a negative-distance impact
  behind the shooter
- target absent from `targets` list (the corpse case) → ray passes
  through

## Order

1 → 2 → 3 (+ test) land as pure shared code with no behavior change.
4 → 5 → 6 turn it on. Each step compiles and runs independently.

**Status (2026-07-31):** 1, 2 and 3 have landed —
`src/shared/weapons.hpp`, `src/shared/player_hitboxes.hpp` and
`src/shared/hitscan.{hpp,cpp}`, all in `game_shared`. Outstanding:
§3's `cast_ray_static`, §7 the test, then 4 → 5 → 6.
