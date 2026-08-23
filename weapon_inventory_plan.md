# Per-weapon state — the inventory, and what is left of it

A weapon's fire clock is a property of the **weapon**, not of the player. One
clock per player measured the interval of whatever was in hand from whatever
fired last, so firing a Scout (1.25s) delayed a Knife swing (0.5s), and swinging
a Knife delayed a Scout that had been holstered and idle for a minute. The bug
was found by asking why quick-switching an AWP to a deagle felt wrong.

Status: **DONE — steps 0–5 all landed (2026-08-23).** The fire-rate bug is
fixed, ammo lives on the weapon, `Weapon_Entity` replicates, the deploy gate
runs on both ends, and the client mirror reproduces both gates. What is left is
in "What this did not do" at the bottom.

---

## The model, and why it is this one

Source keeps **two** clocks and they are not the same clock:

| | Source | here |
|---|---|---|
| per weapon, runs while holstered | `m_flNextPrimaryAttack` | `Weapon_Entity::next_fire_time` |
| per player, set on switch | `m_flNextAttack` | `Inventory::deploy_complete_time` |

The single field this replaced was accidentally doing the **second** job with the
**wrong number** — the incoming weapon's fire interval standing in for a deploy
time. So the fix was never "move the clock onto the weapon"; there were always
two clocks and only one field.

Source 2 carries the same design, with three differences worth knowing. Weapon
state moved off the pawn into `CCSPlayer_WeaponServices` (the viewmodel is a
*separate* service — a class boundary between weapon timing and presentation).
Times became **tick + tick-ratio** (`m_nNextPrimaryAttackTick` +
`m_flNextPrimaryAttackTickRatio`), which is the decomposition `subtick_time_t`
already is. And stats moved into `.vdata`, which is what `weapon_definition_t`
is.

Two things this codebase gets free that Source needs machinery for: uids are
monotonic (`next_entity_id++`, never recycled), so a stale uid resolves to
`nullptr` forever and `CHandle`'s serial number is unnecessary; and the
"services" split is just a component.

**The inventory is STORED, not derived.** An earlier draft had it as "every
`Weapon_Entity` whose `owner_uid` is mine" — no array field needed. That was
rejected: it leaves the invariant with no home, makes "what am I carrying" a
linear walk over the world, and is not what Source does (`m_hMyWeapons` is the
authority; `m_hOwnerEntity` is only the back-reference). Deriving it was chosen
to dodge generator work, which is the wrong reason.

**Keyed by the `Weapon` enum, not a flat list.** This game carries at most one of
each type, so `weapons[active_weapon]` is an index rather than a search, and
`active_weapon` stays the enum that already existed instead of becoming a uid
that could name a weapon the inventory does not hold. Source uses a flat list
because CS carries grenades and multiple slots.

---

## Step 0 — enum-keyed arrays in `def_gen` (DONE)

`u32[Weapon]` parses, resolves and emits as `Enum_Array<Weapon, uint32_t>`. One
production added to the grammar:

```
type -> IDENTIFIER '[' IDENTIFIER ']'   -- element type, keyed by a declared enum
```

Postfix and bracketed so the two parameterised types stay distinct — `<>` sizes a
string, `[]` keys an array — and so the declaration matches the use site.

**It is a front-end feature, and that is the load-bearing property.**
`write_field_rows` expands one array field into one `field_info_t` row per key
(`weapons.Scout`) with a **final offset**, so `write_field`/`read_field`,
`field_to_text`, the change masks and `collect_leaf_fields` have no array case at
all. An array inside a component flattens to `inventory.weapons.Scout` through
the dotted-path join that already existed for components.

Two things that were not obvious going in:

- **Three emitted span lengths came from declared field counts**
  (`COMPONENT_INFOS`, `ENTITY_INFOS`, channel members). An expanded table
  silently disagrees with those — the span either reads past its end or hides
  the tail. They now come from `emitted_row_count()`. The channel one is closed
  off with a guard error instead, since its count is structural.
- **`enum_traits` was emitted after the namespace closed**, but `Enum_Array`'s
  `Indexing_Enum` constraint needs it *before* the first struct that holds one.
  It now emits mid-header via a namespace close/reopen.

Four generator errors, all verified firing: unknown key, key that is not an
enum, array of a component (refused pending a look at how
`capture_field_changes` indexes one), array in a channel.

`entity_layout_test` covers row count, naming order, per-element offset and size,
and the flattened leaf addressing the right element through the entity.

## Step 1 — the inventory exists (DONE)

`Inventory` component on `Player_Entity`: `weapons: u32[Weapon]`,
`active_weapon: Weapon @Networked`, `deploy_complete_time: u64`.
`Player_Entity::active_weapon_id` is **deleted** and all 17 call sites moved.

`Weapon_Entity` gained `owner_uid: u32` and `next_fire_time: u64`.
`Player_Entity::last_fire_slot` is **deleted** — its only reader was the old
gate. `last_fire_tick`/`last_fire_weapon` stay, because `weapon_fire_audio`'s
change detector still wants them.

`src/server/systems/inventory_system.{hpp,cpp}` — `grant_default_inventory`,
`destroy_inventory`, `try_find_active_weapon`.

**Weapons spawn in the SAME TICK as the player**, on both the human path
(`spawn_player_entity_for_client_slot`) and the bot path (`spawn_bot`). This is
not tidiness. A snapshot frame is atomic with respect to loss — a fragmented
message reassembles or is dropped whole, and a client that cannot apply a delta
does not advance `held_snapshot_tick` — so a player and its weapons that spawn
together **arrive** together, and `inventory.weapons` can never name a weapon the
receiver lacks. Split them across ticks and that guarantee is gone.

`drop_client` tears the inventory down *before* the player, since the list of
what to destroy lives on it.

## Step 2 — the gate moves onto the weapon (DONE — this is the fix)

`resolve_player_shot` is now two lines against the weapon's own deadline:

```cpp
if (fire_time < active_weapon->next_fire_time)
    return;
...
active_weapon->next_fire_time =
    shared::subtick_time_after(fire_time, weapon.fire_interval_seconds, tick_dt);
```

`subtick_seconds_between` is out of the fire path entirely.

`inventory_test` guards both directions and was **mutation-checked**: making
`fire_at` stamp every weapon (the old shared clock) fails exactly the two
assertions that encode the bug.

---

## Step 3 — `ammo` moved to the weapon, and `Weapon_Entity` replicates (DONE)

`Player_Entity::ammo` is **deleted**. `snapshot_frame_t` grew a `weapons` map
and `entity_snapshot.cpp` grew its case; `Weapon_Entity::ammo` / `weapon_id` are
live flags now rather than the inert pair TODO.md named.

`Inventory::weapons` became `@Networked` too, which was the part the plan left
implicit. The client resolves its own ammo the way the server does — one index
into the stored forward list — rather than scanning for a weapon whose
`owner_uid` is us. `owner_uid` therefore stayed **server-only**: the plan said to
network it, but nothing over there would read it, and an unread `@Networked`
field is the exact thing the TODO.md note is about. A world model on someone
else's back is what earns it.

Readers moved as listed. Two things the plan did not price in:

- **`place_player_at_spawn` needed the session** *and* the refill stopped being
  one line: magazines are plural, and both fire clocks and the deploy gate are
  absolute deadlines a corpse can leave standing. That is
  `refill_inventory(session, player)` in `inventory_system.cpp`, and it is what
  a respawn calls.
- **`finish_reload` needed the session too**, for the same reason.

The magazine refill on switch is gone. `snapshot_delta_test` guards the wire
half: four records for a player and its three weapons, one for a shot, and
**one for a switch** — the last of which is the cheese, written down as a
record count.

## Step 4 — the deploy gate (DONE)

`deploy_duration_seconds` is a column in `weapon_definition_t`: Knife 0.4,
Scout 0.7, Rocket Launcher 0.9. Authored here, not derived from an animation
length — see the comment on the field for why that direction matters.

Written at the switch in `server_impl.cpp`'s step loop, from the **incoming**
weapon at `step_time`, and checked in `resolve_player_shot` beside the weapon's
own clock. Two gates, and the test asserts the second replaces rather than
extends the first — the Knife is quicker than the Scout, so interrupting a Scout
deploy with a Knife is genuinely ready sooner. That is what storing a deadline
means, and it is what Source does.

**`shared::try_weapon_selected_by` fell out of this.** The key→weapon mapping
was a chain of ifs in the server's step loop, and the client now needs the same
answer to know what deploy time to charge. Spelled twice it would be two
answers, so it is one table in `weapons.hpp`. It also fixed a divergence that
predates all of this: the client cancelled its predicted reload on **any**
number key, including unbound ones the server ignores.

## Step 5 — the client mirror (DONE)

`prediction.seconds_since_local_fire` is `Enum_Array<Weapon, float>` — one clock
per weapon, all advancing, because the server's do. One float was the client
half of the original bug.

`seconds_until_local_deploy_complete` beside it is the deploy gate, started off
the same edge and the same table the server switches on. Both are cleared on
death, like the reload clock, because `refill_inventory` clears the server's.

One knowingly-wrong case, documented at the site: the switch predicate compares
against the **replicated** `active_weapon`, which is a round trip stale, so
switching faster than a round trip can charge a deploy the server does not. The
fix is a predicted copy of `active_weapon`, which is a second answer worth more
than it buys while this is audio and a debug readout.

## The deploy timer on screen

`cl_show_deploy_timer` (client, default off) draws the seconds left on a switch:
`client/hud/deploy_timer.{hpp,cpp}`.

**It is NOT an announcement, and that is the whole design decision.** An
announcement is a discrete occurrence pushed into a model with a lifetime — set
once, counts itself down, and the model is what gets drawn. A deploy countdown
is a continuous read of a clock that already exists, so pushing it into a banner
would be a second copy of the gate, free to disagree with the one the shot is
judged against. It is polled every frame from
`prediction.seconds_until_local_deploy_complete`, unconditionally, and zero
draws nothing. See the "continuous values are polled from the truth" rule in
CLAUDE.md.

It reads the CLIENT's predicted clock rather than
`Inventory::deploy_complete_time`, which is the only honest option: that
deadline is sub-tick and server-only, and a replicated copy would be a round
trip behind the keypress the player is watching the number for.

Off by default because it draws a NUMBER, and a number is not what a shipped HUD
shows for this — the draw animation is. It exists because the gate is otherwise
invisible from inside the game: the shot simply does not happen, which is
indistinguishable from a broken fire path at the only moment anyone looks.

---

## What this did not do

- **Reload is not blocked during a deploy.** The gate is on the fire path only,
  which is where the plan put it. CS blocks both; adding it is one condition in
  the same step loop.
- **No draw animation, no viewmodel.** That was always the point of landing this
  first: with the timing owned by `weapon_definition_t`, an animation is
  authored against a number that already exists rather than becoming the number.
- **The deploy gate has no test that reaches `server_impl.cpp`.**
  `inventory_test` reimplements the two-clock gate, like it already did for the
  one-clock one — the fire path is not extractable without a server context.
  Mutation-checked: dropping the deploy term from the helper fails five
  assertions.

---

## Things not to undo

- **`last_fire_tick` stays per-player and `@Networked`.** It is
  `weapon_fire_audio`'s change detector and "the last shot, whichever weapon" is
  what that reader wants. Only the *gate* went per-weapon. `last_fire_weapon` is
  a **value, not a reference**, for the same reason: it outlives the weapon it
  names.
- **`reload_complete_time` stays on the player.** Switching cancels the reload
  (matches CS), so it only ever exists for the held weapon.
- **Do not add a weapon state enum.** Layer 1 is timestamps. See
  `project_weapon_presentation_layers` in memory and the three-layer split there.
- **The sniper bolt is presentation.** Scout's `fire_interval_seconds = 1.25`
  conflating shot + bolt is what Valve does too.
