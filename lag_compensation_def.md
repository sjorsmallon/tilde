# done

# Lag compensation: rewind the hit test to what the shooter saw

> **Status: LANDED (2026-08-16).** Everything below is in the tree: the three
> proto fields, the three cvars, `src/shared/lag_compensation.{hpp,cpp}`,
> `src/test/lag_compensation_test.cpp` in `GAME_TESTS`, deferred damage, and the
> deleted `header.timestamp` sort.
>
> `hitscan_plan.md` deferred this deliberately: *"`resolve_hitscan` takes
> candidate positions as parameters so rewind later changes the caller, not the
> function."* That prediction held exactly — §2 changed the caller and
> `hitscan.cpp` was not touched.
>
> **Four things the build did differently, all in §2/§3's shape rather than
> against it:**
>
> - **The bracket check is a shared, pure function**, not a server-side
>   `clamped_bracket(context, move)`. §4 asks for tests of the
>   past-`held_snapshot_tick` refusal and the over-clamp, and a test linking
>   `game_shared` alone cannot reach into `server_impl.cpp` — so the decision
>   lives in `shared::classify_bracket(requested, held, current, max_rewind)`,
>   returning a `bracket_status_t` (Ok / Clamped / Absent / Malformed / Unheld).
>   `get_interpolation_bracket_for_move` in `server_impl.cpp` is the thin server half that
>   reads the cvars and does the logging.
> - **`posed_players_t` and `pending_hit_t` sit in different places.**
>   `posed_players_t` moved to `shared/lag_compensation.hpp` as §2 says. The
>   rewind scratch went *beside* `posed_players` above the reset-scoped section
>   rather than into a per-tick group — it is rebuilt whole before every read, so
>   the same "nothing resets it" reasoning applies verbatim. Only
>   `pending_hits` is a group member (`tick_output_t`), and
>   `server_context_test` asserts both halves of it.
> - **`damage_info_t` moved to `src/server/damage_types.hpp`.** The context now
>   holds a list of them, while `damage.hpp` includes the context for
>   `inflict_damage`'s parameter — one direction had to give, and the plain value
>   is the half with no dependencies.
> - **Deleting `header.timestamp` moved `Packet`'s payload offset.** The header
>   had been 8-aligned by accident, and the send path computed the payload's
>   offset as `sizeof(Packet_Header) + sizeof(int)`, which stopped naming
>   `offsetof(Packet, buffer)` the moment the alignment changed — every send
>   shipped from the wrong offset and `udp_socket_test` caught it. The offset is
>   now a stated constant with a `static_assert` against `offsetof`.
>
> **What is NOT covered:** §4's mutual-trade test. The fix is `Tick()` deferring
> damage, and `Tick()` is in the `game_server` DLL behind no exported entry
> point, needing Jolt, a map and a socket — there is no function for a test to
> call. It is in `todo.md` as an open item; §5's live check is the coverage
> today.
>
> **Two things here were argued to and reversed. Both are load-bearing:**
>
> 1. **The wire carries the client's interpolation BRACKET, not a single
>    collapsed moment.** The obvious simplification — have the client send one
>    fractional tick and let the server bracket it against its own denser history
>    — is *wrong*, and §1 "the chord vs. the truth" says why. §4's
>    chord-not-truth test exists to stop it coming back.
> 2. **Prediction-ahead needs no wire field at all.** `command_number` already
>    covers it; the server re-derives the shooter's own position from the inputs
>    in the same message. Only the interpolation cursor is unrepresented.

## Context

The server tests where a target is **now**; a shooter on 80 ms aimed at where it
was ~5 ticks ago. `todo.md` already names this gap, and `animation_def.md` §4
calls it *guarantee 2* — one machine agreeing with its own past.

Three things make the gap worse than a plain tick offset:

1. **The move sort key is dead.** `server_impl.cpp:713-715` sorts `inbox.moves`
   by `packet.header.timestamp`, which nothing in the codebase ever writes
   (TODO at `udp_socket.cpp:253`). `std::sort` is not stable, so intra-tick move
   order is arbitrary — a line that reads deterministic while being a
   nondeterminism source. Sorting was never lag compensation anyway: it orders
   moves *within* a tick, while lag compensation is about *which world state*
   each move is tested against. Every move in tick N tests the same
   `pose_all_players` result regardless of order.

2. **Damage lands mid-loop.** `inflict_damage` is called inside the move loop
   (`server_impl.cpp:931`) and the loop gates on `is_dead` at its top
   (`:757`). So when A and B shoot each other in the same tick, whichever move
   happens to sort first kills the other and the second shot is silently
   dropped by the dead gate. The arbitrary order from (1) decides who wins.

3. **`held_snapshot_tick` is not the moment the shooter saw.** It is the newest
   snapshot the client fully *received*. What was on *screen* is a lerp from
   `snapshots[0]` toward `snapshots[1]`, with `interpolation_time` reset on each
   arrival (`play_state.cpp:1244-1273`) — so the world the crosshair was on sits
   between tick `H-1` and `H`, at neither. Rewinding to `H` alone is
   systematically up to a full tick too new, and more when a drop widens the
   bracketing gap. Nothing on the wire describes that blend today, which is why
   §1 adds fields rather than reusing this one.

**The policy this commits to, stated plainly:** shooter-favored. "I hit what my
crosshair was on." The bill is that a victim already behind cover on their own
screen *and* on the server's can still take damage. That is not a defect to fix
later — it is the price of the guarantee, and it is bounded by clamping how far
back a client may reach.

The alternative is not "no tradeoff", it is the mirror one: without rewind, a
shooter must lead every target by their own ping — 32 units at 80 ms and
400 u/s. Every shipped shooter picks shooter-favored, because bad aim feedback
is felt on every shot and dying-behind-cover is felt occasionally.

**What the codebase already gives us for free:**

- `resolve_hitscan` (`shared/hitscan.cpp:6`) takes a `Span<const posed_hitbox_t>`
  per target and knows nothing about skeletons — the rewind entry point exists
  unchanged, and its header says so at `hitscan.hpp:15-22`.
- `network::snapshot_frame_t` (`shared/network/entity_snapshot.hpp:85-100`)
  stores **whole `Player_Entity` values by copy**, 32 ticks deep, globally
  (`server_context.hpp:84-87`). Every input `player_pose_t` needs — `position`,
  `body_yaw`, `view_angle_yaw`, `view_angle_pitch` — is in there.
- Because history frames are immutable copies, **there is nothing to restore**.
  We build a temporary pose set instead of mutating and un-mutating the world,
  which is strictly cleaner than Source's save/restore.
- The world BVH is static, so geometry needs no rewind at all.

**Why movement needs no rewind, which is not the same question.**

It reads like an inconsistency — a move is applied to the present tick, a shot is
judged against a past one — so it is worth saying why it is not one. The dividing
line is not movement vs shooting; it is **static vs dynamic**, and movement
happens to touch only the static half.

`player_move` (`shared/player_move.hpp:106`) takes a `Bounding_Volume_Hierarchy`
and nothing else about the world: no players, no physics bodies, no tick number.
Walls are identical at every tick, so "which tick did I collide against" is a
question with no content. Shooting is the only path that tests against other
players, which is the only thing in the world that differs between two ticks.

Stated as the symmetry it actually is, prediction and rewind solve one problem
from opposite ends:

- **You own the inputs → predict.** The client runs the identical `player_move`
  on the identical inputs with the identical `@Mirrored` `pm_*` values, so it
  computes the answer itself and the server merely confirms. Nothing to rewind —
  the client is already at the latest tick.
- **You do not own the inputs → rewind.** A hit is not the client's to decide, so
  instead of moving the client forward, the server moves itself backward.

**The invariant is that movement never touches anything dynamic, and today it is
enforced by that signature** — you cannot collide against a player through a
parameter you were not given. Nothing asserts it, because nothing needs to yet.

It breaks silently the day movement touches something that changes over time, so
the cases are worth naming in advance:

- **Player-vs-player collision.** Movement would then depend on where others
  are — exactly the ambiguous quantity. Predict against the ones the client
  *drew* (past) while the server resolves against present, and prediction is
  wrong every frame two players touch; resolve against present on both ends and
  the client cannot predict at all, because it does not know present. There is no
  clean answer, which is why plenty of shooters ship with no player-player
  collision, or make it a server-side soft push and accept the mismatch.
- **Moving platforms, doors, elevators, standing on a `Physics_Body_Entity`.**
  Same shape.
- **Geometry that moves at all.** That breaks the *rewind* rather than the
  prediction: players would be rewound and the door would not, making "I shot
  through a door that was open back then" representable.

None of this is planned. The note exists so that whoever adds the first dynamic
collider finds out here rather than from a prediction drift with no error in it.

---

## 1. Wire: the client sends the blend it was aiming through

**The guarantee, stated exactly, because it decides the wire format:**
*reproduce what was on screen* — not *reproduce what was true*. Those differ,
and the difference is why the bracket ships rather than a single moment.

Say the client holds ticks 300 and 304 (301–303 were lost) and is lerping at
`t = 0.25`. It drew a **straight line** from state@300 to state@304. The true
tick-301 state is not on that line whenever the target strafed, jumped or turned
in between. Send the collapsed moment "301.0" and the server poses the truth,
the shot misses a crosshair that was dead on the drawn model, and the player
gets the exact "I hit that, no reg" this change exists to kill. So the server
must lerp **the same two endpoints the client did**, which means the endpoints
go on the wire.

**Two clocks, and only one of them needs a field.** The client runs its own body
*ahead* of the server (prediction) and everyone else *behind* it (interpolation).

| clock | at server tick 305, client on 80 ms | already on the wire as |
|---|---|---|
| prediction — own body, runs **ahead** | ~310 | `command_number` |
| render — everyone else, runs **behind** | 300.4 | nothing, until §1 |

Prediction needs nothing added: the move carries the inputs, the server applies
them at `server_impl.cpp:825` and takes the eye from the result at `:866`, and
`command_number` already reconciles the drift. Only the interpolation cursor is
unrepresented, and these three fields are it.

`proto/game.proto`, on `C2S_PlayerMoveCommand` (append after
`held_snapshot_tick = 8`):

```proto
  // The blend this move was aimed THROUGH: remote players are DRAWN
  // interpolated between two snapshots (play_state.cpp:1253), never at either
  // one, so the world the crosshair was on is at no whole tick.
  //
  // The server lerps these SAME two endpoints before testing a shot, rather
  // than posing the true state at the moment they work out to. After packet
  // loss the two are different places -- the client drew a chord, and the real
  // path between the endpoints may have curved off it. Reproducing the chord is
  // what makes "if it was under your crosshair, you hit it" true.
  // See shared/lag_compensation.hpp.
  //
  // Distinct from `command_number`, which stamps the same press on the client's
  // OTHER clock: prediction runs its own body AHEAD of the server while
  // rendering everyone else BEHIND it. That clock needs no compensation -- the
  // server re-derives the shooter's own position from the inputs in this very
  // message.
  //
  // interpolated_towards_tick == 0 means no blend yet (spectating, or fewer
  // than two snapshots seen); the server falls back to present-tick poses.
  optional uint32 interpolated_from_tick    = 9;
  optional uint32 interpolated_towards_tick = 10;
  optional float  interpolation_fraction    = 11;   // 0..1, from -> towards
```

**`interpolated_towards_tick` is deliberately not merged into
`held_snapshot_tick`,** even though the two are equal today — `snapshots[1]` is
always the acked tick, because absent players are erased rather than retained
(`play_state.cpp:861-864`). They answer different questions ("what did I
reconstruct" vs "what did I draw"), and the equality is an invariant of the
client's publish path, not of this message. It stops holding the moment anything
replicates at its own cadence, which `todo.md` already expects.

**Client side.** `replication_t` (`src/client/client_context.hpp:248`) gains one
local member, `previous_snapshot_tick`, latched beside the `interpolation_time`
reset at `play_state.cpp:853` and the `snapshot_history.acknowledge` at
`:866-867`. It is the tick `snapshots[0]` came from — the only piece of the
bracket the client does not already have to hand. Cleared with the rest of the
group by whichever reset owns `replication_t`.

Fill the three fields at the move build site, `play_state.cpp:1135`, from
`previous_snapshot_tick`, `snapshot_history.acked_tick`, and the same clamped
`interpolation_time / interp_duration` the interpolation loop computes at
`:1244-1251` — so the reported blend is the drawn blend by construction.

Two accepted inaccuracies, both worth a comment at the site:

- Moves are built in the fixed-step accumulator loop (`:1120`), which runs
  *before* the interpolation advance (`:1273`), so the fraction is last frame's.
  Under a frame of error against tens of milliseconds of network delay.
- `interpolation_time` is **global**, not per remote player
  (`client_context.hpp:251`), so one bracket describes every target. True today
  — no PVS, everything arrives in one packet — and already flagged as fragile in
  `todo.md`. If per-entity cadence lands, this field set becomes per-entity or
  becomes wrong; note that in the same place.

---

## 2. Shared: pose players across a past bracket

New `src/shared/lag_compensation.{hpp,cpp}` — takes values, not contexts
(house preference), so the test links `game_shared` alone and it never sees
`server_context_t`:

```cpp
// The blend a client was drawing through, named in server ticks. Not a moment:
// `from` and `towards` may be several ticks apart after packet loss, and the
// straight line between them is what was on screen -- see the field comments in
// game.proto for why the chord and not the truth.
struct interpolation_bracket_t
{
  uint32_t from_tick    = 0;
  uint32_t towards_tick = 0;
  float    fraction     = 0.f;   // 0..1
};

// Poses every living player as the client drawing `bracket` saw them, into
// caller-owned storage. Fallible: either endpoint may have aged out of the
// ring, which is the caller's business -- it falls back to the present-tick
// pose set.
[[nodiscard]] bool try_pose_players_across_bracket(
    const network::Snapshot_History<network::snapshot_frame_t>& history,
    const shared::player_rig_t&                                 rig,
    const aim_settings_t&                                       settings,
    interpolation_bracket_t                                     bracket,
    posed_players_t&                                            out);
```

`posed_players_t` moves out of `server_context.hpp:101-109` into this header so
both the live and rewound paths produce one type.

Body, mirroring `pose_all_players` (`server_impl.cpp:440-482`) but reading
frames instead of the live entity pool:

- `history.find(from_tick)` and `find(towards_tick)`; a miss on either → return
  `false`. Do **not** substitute a nearby tick: a bracket the server cannot
  reproduce exactly is one it should decline, and the present-tick fallback is
  honest about that where a near-miss would not be.
- Walk the `from` frame's `Player_Entity` map. **Liveness comes from the `from`
  frame** — that is what the client was drawing at the start of its blend. A uid
  absent from the `towards` frame (it despawned mid-blend) is posed from `from`
  alone rather than skipped.
- Lerp `position` and `view_angle_pitch` plainly; `body_yaw` and
  `view_angle_yaw` take the **short way round** via `linalg::wrap_degrees`,
  exactly as the client's own interpolation does at `play_state.cpp:1259-1271` —
  a raw lerp across the ±180 seam is the bug that loop already carries a comment
  about.
- Size `out.volumes` fully before pushing any target, for the same
  span-invalidation reason as `pose_all_players:455-462`.
- Feed the lerped values to `shared::compute_player_hitboxes` — the *same*
  function the live path and the client's `debug_show_hitboxes` overlay call, so
  the rewound silhouette is the drawn silhouette.

Excluding the shooter needs nothing: `resolve_hitscan` already takes `ignore_uid`.

---

## 3. Server: rewind at the shot, defer the damage

**cvars** (`src/shared/cvars/cvars.def`, beside `sv_aim_*` around `:103`):

```
sv_lag_compensation:       bool = true   @Server  "Rewind targets to what the shooter saw"
sv_max_rewind_ticks:       i32  = 12     @Server  "Cap on how far back a shot may reach (~200ms at 60Hz)"
sv_lag_compensation_debug: bool = false  @Server  "Log every rewind: requested bracket, clamped bracket, disparity, hit"
```

`@Server`, not `@Mirrored` — the client makes no decision from these. Clamp
against `min(sv_max_rewind_ticks, Snapshot_History::CAPACITY - 1)`; a request
older than that is pinned to the boundary, not rejected.

**Context** (`src/server/server_context.hpp`): a scratch `posed_players_t`
reused per shot, and the pending-damage list. Both belong to the per-tick
`incoming`/`outgoing` grouping — put them where `clear_incoming` /
`clear_outgoing` (`server_context.cpp`) already run, `clear()` per member so the
vectors keep capacity at 60 Hz, and extend `server_context_test` to assert both
halves as that file's convention requires.

**Fire path** (`server_impl.cpp:862-988`), replacing the
`context.posed_players.targets` argument at `:911-912`:

```cpp
const interpolation_bracket_t bracket = clamped_bracket(context, move);
Span<const shared::hitscan_target_t> targets = context.posed_players.targets;

if (*context.cvars->sv_lag_compensation && bracket.towards_tick != 0 &&
    try_pose_players_across_bracket(context.replication.snapshot_history, rig,
                                    settings, bracket, context.rewind_scratch))
  targets = context.rewind_scratch.targets;
// else: fall through to the present-tick set -- spectating, the first shots
// before two snapshots exist, or a bracket that aged out of the ring.
```

**Validating the bracket — the server must not rewind through a blend the client
could not have been drawing.** `clamped_bracket` runs these in order, and every
rejection falls back to the present-tick pose set rather than proceeding on a
repaired value:

1. **`towards_tick <= client.held_snapshot_tick`.** A client cannot have drawn a
   snapshot it never told us it holds. This is the check that ties the two
   fields together and it is the only one that catches a *fabricated* bracket —
   the ring bounds alone would happily accept a tick the server sent to nobody.
   Sound against UDP reordering for free: `held_snapshot_tick` only ever grows
   (`std::max`, `:726-727`) and the drain pass runs over every move *before* the
   move loop, so a late-arriving move is compared against a high-water mark that
   already includes it. `log_warning` with slot and both values — a violation is
   a malformed or hostile client, not a routine miss.
2. **`from_tick <= towards_tick`**, and `fraction` within `[0,1]`. Malformed;
   reject rather than repair.
3. **Both endpoints resolve in the ring** — that is
   `try_pose_players_across_bracket` returning `false`, and it is the one
   ordinary miss of the four: a client stalled past ~530 ms lands here
   legitimately.
4. **`from_tick >= tick_number - max_rewind`**, the policy clamp.

**When the clamp in (4) bites, say so.** A silently clamped rewind judges the
shot through a blend the shooter did not aim through, and they feel it as
no-reg — a clamp that logs nothing is the silent failure this codebase refuses.
`log_warning` naming the slot, the requested bracket, the clamped one, and the
disparity `tick_number - towards_tick` in ticks and milliseconds.

Rate-limit it, or a client sitting at 300 ms warns on every shot and buries
everything else: a `last_rewind_warning_tick` on `client_slot_t` (beside the
other server-side notes about the client), at most one warning per second per
slot. The disparity is worth surfacing even when nothing is clamped —
`sv_lag_compensation_debug` logs it per shot, the warning fires only past the
clamp.

Those four checks plus the clamp are what turn trust into *bounded* trust: a
liar can then only name a blend an honest laggy client could also have named,
out of snapshots it demonstrably received.

Read the bracket off **this move** (`tm.move.interpolated_*()`), never off
`client_slot_t`. The drain pass at `:720-728` folds `held_snapshot_tick` into a
per-client `std::max` high-water mark; the same fold applied to a bracket would
judge the shot through a *newer* blend than the shooter aimed through, which is
the exact error being removed.

Keep `pose_all_players` and its `built_for_tick` `fatal_error` (`:906-909`)
exactly as they are — they now guard the fallback arm, which is still a real arm.

**Deferred damage.** The fire block stops calling `inflict_damage` and instead
pushes `{damage_info_t, impact_point, impact_normal, region, hit_uid}` onto the
pending list. A second pass immediately after the move loop closes (`:989`,
before `update_bots`) drains it: `inflict_damage`, the `Flesh_Impact` effect,
and the `last_hit_tick` / `last_hit_was_headshot` latch.

The trade fix falls out with no extra bookkeeping and this is worth a comment at
the site: once no damage lands during the loop, `player->health` is not mutated
during it either, so the existing `is_dead` check at `:757` already reads
*start-of-tick* health. You cannot shoot if you were dead before this tick; a
kill landing this tick no longer retroactively cancels your own shot.

**Move ordering** (`:713-715`). Delete the sort on the never-written
`header.timestamp`, and delete the field from `Packet_Header`
(`packet.hpp:104`) together with the TODO at `udp_socket.cpp:253-259` — a sort
key nobody writes is exactly the silent failure this codebase refuses. Replace
with a deterministic `(client_slot, command_number)` sort; cross-client order
stops carrying meaning once damage is deferred, so nothing needs to invent a
cross-client clock. In the loop, drop any move whose `command_number` is not
greater than `client.latest_processed_command` — that rejects duplicates and
UDP-reordered replays, which the codebase does not do today at all.

**Explicitly out of scope, and it belongs in `todo.md` as this item's
successor:** there is still no cap on moves per client per tick — a client whose
packets bunch after a stall gets N `player_move()` steps in one tick. Firing is
interval-gated so this is a movement exploit, not a damage one, and the
stale-command drop above removes the replay half of it.

---

## 4. Tests

New `src/test/lag_compensation_test.cpp`, added to the target list **and** to
`GAME_TESTS` at the bottom of `CMakeLists.txt` (written out, not globbed):

- Frame lerp at fractions 0 / 0.5 / 1 over two synthetic `snapshot_frame_t`.
- Yaw and `body_yaw` across the ±180 seam take the short way.
- A uid present only in the `from` frame is posed from it, not skipped.
- A uid dead in the `from` frame is skipped.
- Ring miss and over-clamp: a bracket older than `sv_max_rewind_ticks` clamps to
  the boundary; one older than the ring returns `false`; `towards < from` is
  rejected.
- **A bracket naming a tick past `held_snapshot_tick` is refused** and falls back
  to present-tick poses — the fabricated-bracket case, which the ring bounds
  alone would accept.
- **The chord, not the truth** — the test the whole wire format exists for. Give
  a target a curved path across ticks 300→304 (a strafe), request the bracket
  `{300, 304, 0.25}`, and assert the pose lands on the straight line between the
  endpoints and *not* on the stored tick-301 state. Those two must be far enough
  apart in the fixture that a regression cannot pass both. **This is the guard
  against re-deriving the collapsed-moment design the status block warns about.**
- End-to-end, the case that motivates the whole change: a target crossing at
  speed, one ray that **misses** the present-tick pose and **hits** the rewound
  one, plus the converse — the shot that would hit "now" but not "then" must
  miss, or the fix is one-sided.
- Trade: two shooters resolving against each other in one tick both land damage.

`server_context_test` gains the clear/survive assertions for the two new
per-tick members. `hitscan_test` and `hitbox_rig_test` should be untouched — if
either moves, the rewind changed the hit math, which it must not.

---

## 5. Verification

```bash
cmake --build cmake_build -j8
ctest --test-dir cmake_build -j8                       # full suite
ctest --test-dir cmake_build -R lag_compensation --output-on-failure
```

The proto changes both sides, so a stale executable will not interoperate —
rebuild all three launchers, not just `MyGame`.

Live check, which needs the real network path rather than the integrated build:
run `MyGame_Server`, connect `MyGame_Client`, `spawn_bot chase`, and set
`sv_lag_compensation_debug 1`. Fire at a moving bot and compare the logged
requested vs clamped bracket against the debug overlay. Then toggle
`sv_lag_compensation 0` mid-session — that A/B is the whole point of it being a
cvar, and leading the target should visibly become necessary again.

To exercise the clamp warning without a lossy network, drop `sv_max_rewind_ticks`
to 1: every shot from a real client then over-clamps, which proves the warning
fires, carries the disparity, and rate-limits to one per second per slot rather
than one per shot.

## 6. Docs to update when this lands

- `todo.md` — close the lag-compensation item, and add the moves-per-tick cap as
  its successor.
- `animation_def.md` §4 — mark guarantee 2 as landed for `position` / `body_yaw`
  / view angles, noting that `locomotion_phase` still owes its accumulator.
- `CLAUDE.md` "Player hit volumes" — the last line is currently *"Not built yet:
  lag compensation. The server tests where the target is now."* Replace it with
  the rewind path and the shooter-favored policy, so the tradeoff is written
  down where the hit test is documented.
- This file's status block — flip it to LANDED, the way `events_def.md` and
  `cvar_def.md` do.
