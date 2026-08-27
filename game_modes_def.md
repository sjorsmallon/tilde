# Game Modes — Design

Outcome of the design discussion on 2026-08-25. Sibling of `cvar_def.md` and
`entity_def.md` in the sense that matters: closed sets, single declaration
point, derive-never-invent, loud failures. None of that is re-argued here.

The subject is narrow on purpose. This is about **how a deathmatch and a
CS-style round mode can be the same code**, and specifically about what shape
the difference between them takes. It is not about what modes to ship.

## Where the code actually is

The phase FSM is written and correct. `game_rules_state_t` holds
`{phase, phase_end_tick, round_number, max_rounds}`;
`server/systems/game_rules_system.cpp` has `enter_phase` as the one writer of
`phase`, `next_phase` as the chain, `start_match` / `end_round` as the two
outside-cause transitions, and three gates. `Round_Phase` is declared in
`events.def` because it is on the wire, and `Round_Phase_Changed` carries every
transition on the reliable channel, where the client turns it into an
announcement.

What is missing is not structure, it is **callers**. As of today
`can_take_damage` and `is_round_live` have none, and `is_movement_allowed` has
one. `Team_Allegiance` is declared in `entities.def`, sits on both
`Player_Spawn_Entity` and `Player_Entity`, and is referenced by no `.cpp` in
the tree. There is no score anywhere, and `CmdConnect::player_name` is received
and logged and then dropped on the floor.

So the FSM is a skeleton with no muscle attached, and the question "what shape
do modes take" is being asked at exactly the right time: before the muscle is
attached to it in a shape that only fits one mode.

## The seven decision points

Enumerated by diffing a deathmatch against a CS-style round mode. This list is
the whole design input; everything below is a consequence of it.

| # | Where | Deathmatch | Rounds |
|---|-------|-----------|--------|
| 1 | player dies | schedule a respawn | mark eliminated, stay dead |
| 2 | win condition, each Live tick | frag limit reached, match over | one team left alive, end the round |
| 3 | entering the per-round freeze | there isn't one | respawn all, freeze, restore loadout |
| 4 | the phase chain | Warmup, Live, Game_Over | the existing five |
| 5 | spawn selection | farthest from enemies | this player's team markers |
| 6 | player joins | spawn now, no team | assign to the smaller team, spectate until next round |
| 7 | kill award | +1 frag | +1 frag |

Seven is the number that decides the shape. It is too many for `if (mode ==
Deathmatch)` scattered at the sites, and nowhere near enough to justify a
scripting runtime. It is squarely in the range where the only real question is
*data or vtable*.

## Shape: what was rejected, and why

### A scripting VM (QuakeC, Q3VM)

Rejected outright, and the reason is worth writing down because the pull comes
back every time the switch count grows.

A VM buys exactly one thing: **game logic that can change without recompiling
the engine, authored by someone who cannot be trusted or cannot be reached.**
Quake shipped QuakeC for that. Q3 went further and shipped a bytecode VM
because Q3 runs mod code *on the client* — `cgame` and `ui`, auto-downloaded
from whatever server you joined — so the VM is as much a sandbox against
hostile servers as it is a portability layer across the platforms of the day.
Q3 also supports plain native game DLLs when neither property is needed.

This project has no untrusted author, no auto-download, no second platform for
game logic, and a recompile is free. Every one of the VM's purchases is
something already had for nothing. What remains is the cost: a language, an
interpreter, a debugger you do not have, and a permanent seam that error
messages cannot cross.

The failure mode to watch for is not "someone proposes writing a VM." It is
**building one in installments** — a condition tree for win conditions, an
expression evaluator for scoring, a small DSL for phase chains. Each step looks
like data-driving one decision. The sum is a VM with none of the tooling. The
line that holds is stated below.

### A struct of function pointers

This is the shape the seven decision points *feel* like they want: a
`game_mode_vtable_t` with seven members, one `constexpr` instance per mode.
It groups by mode, so "what is Rounds" reads top to bottom in one place instead
of being scattered across seven switches. That is a real advantage and it is
why the idea keeps surfacing.

It is rejected on **which axis the compiler polices**, which is settled by
which axis actually churns. There will be two or three modes, ever. Decision
point #8, #9 and #10 will be discovered continuously for as long as the game is
being built. So the shape must make *adding a decision point* safe — and that
is precisely where the vtable fails. Adding a member means every existing table
silently gets a null pointer through aggregate initialisation, and C++ offers
no way to require that an aggregate member be provided.

That hole is already documented one level down, in `array.hpp`: `Enum_Array`
"fixes the length but does not check you filled it", which is why
`rows_in_enum_order` exists and why every hand-written table of enum-indexed
data carries a `static_assert`. A vtable reintroduces the identical hole on the
axis it was introduced to protect.

### Virtuals (Valve's `CGameRules`)

The one shape where attaching behavior to the struct genuinely pays: a pure
virtual makes a new decision point a compile error in every mode, which is the
guarantee the function-pointer table cannot give. `CGameRules` to
`CHalfLifeMultiplay` to `CHalfLifeTeamplay` is not a mistake, and GoldSrc was
right to prefer it to QuakeC.

It is rejected here for cost rather than correctness — a base pointer, a
lifetime, a heap allocation for one object — and for one structural reason that
also condemns it on its own terms, below.

### A bag of cvars with no name

`mp_teamplay 1`, `mp_respawn 0`, `mp_freezetime 3` and no `Game_Mode` at all.
This is how a lot of Source-era configuration actually worked, and it is
tempting because `attached_cvars` already exists and already reverts on map
unload.

Rejected because a bag of independent flags makes **illegal combinations
representable**, and there is nowhere to say so. `mp_respawn 1` together with
elimination-based round ends is a round that can never finish. A named mode is
the thing that says which combinations are real.

## Shape: the value table

Most of the pull toward attaching behavior dissolves once each difference is
written as a **noun instead of a verb**. The table above describes all seven as
verbs, which makes all seven look behavioral. They are not:

| # | As a verb | As a noun |
|---|-----------|-----------|
| 1 | schedule a respawn / stay dead | `bool respawn_during_round` |
| 2 | frag limit / last team standing | `Win_Condition` |
| 3 | respawn all and freeze | falls out of #4 |
| 4 | which phases run | `Span<const Round_Phase>` |
| 5 | farthest from enemies / team markers | `Spawn_Policy` |
| 6 | assign a team, spectate until next round | `bool auto_assign_teams`, `bool join_in_progress` |
| 7 | award a frag | not a difference at all |

Five of the seven are flags. One is not a difference. Two are genuinely
behavioral — and those two become **narrow enums that are not the mode enum**,
which turns out to be the whole payoff.

So a mode is a row of values:

```cpp
enum class Game_Mode     { Deathmatch, Rounds };
enum class Win_Condition { Frag_Limit, Team_Elimination };
enum class Spawn_Policy  { Farthest_From_Enemies, Team_Markers };

struct game_mode_settings_t
{
    Game_Mode     key;
    Win_Condition win_condition;
    Spawn_Policy  spawn_policy;
    bool          respawn_during_round;
    bool          auto_assign_teams;
    bool          join_in_progress;
    Span<const shared::Round_Phase> phase_chain;
};

constexpr Enum_Array<Game_Mode, game_mode_settings_t> GAME_MODES = { ... };
static_assert(rows_in_enum_order<&game_mode_settings_t::key>(GAME_MODES));
```

plus exactly two switches, on `Win_Condition` and on `Spawn_Policy`. Neither
switches on `Game_Mode`.

`game_mode_settings_t` and not `_params_t` or `_config_t`: it is the values a
function needs, handed to it, which is the same convention `player_move` and
the `sv_aim_*` extents already follow.

### Why the narrow enums are the point

A third mode can be `Team_Elimination` plus `Farthest_From_Enemies` — a
combination neither shipped mode uses — with no new code anywhere. A vtable or
a subclass makes each mode a **closed bundle**, so every novel recombination is
a new class overriding six methods it did not want to change. That is the
structural argument against `CGameRules` on its own terms, and it is why most
"modes" in Source games were `mp_*` combinations rather than subclasses.

The value table gets the recombination for free, and the named mode row keeps
the illegal combinations out. Those two properties are in tension in every
other shape considered here.

### Where each decision point lands

1. **Death.** `respawn_system`'s drain already takes the delay as a value it is
   handed rather than a cvar it reads, which is the pattern. The death site
   consults `respawn_during_round` before calling `schedule_respawn`.
2. **Win condition.** One function, `check_win_condition(context, settings)`,
   called each Live tick, switching on `Win_Condition`. It calls the existing
   `end_round` — the seam is already there and already refuses to fire outside
   Live, so a double-fire from two checks in one tick cannot skip a phase.
3. **Round-start respawn.** Stays in `enter_phase`, exactly where its NOT
   WIRED YET comment already says it belongs. It is the one thing in the whole
   system that genuinely cannot be a gate.
4. **Phase chain.** `next_phase` becomes an index step along `phase_chain`
   instead of a switch. The `Round_End` branch on `round_number >= max_rounds`
   survives as the one real branch.
5. **Spawn selection.** `try_pick_human_spawn` grows a `Spawn_Policy`, and the
   `team_allegiance` already sitting unread on `Player_Spawn_Entity` finally
   gets a reader.
6. **Join.** The `start_match` guard's reasoning already anticipates this: a
   player leaving and rejoining walks the count 4, 3, 4 and must not restart
   the match.
7. **Kill award.** Nothing. It is the same in both.

### Durations become the cvar half

`game_rules_system.hpp` already says where this goes: *"if per-mode or per-map
durations land, route these through cvars or a mode config rather than growing
branches here."* Take the first option. `mp_round_seconds`,
`mp_countdown_seconds`, `mp_frag_limit`, `mp_max_rounds` are cvars;
`map_respawn_delay_seconds` already is one.

Which means **a map picks its mode and tunes it through `attached_cvars`**, with
the Map Cvars panel as the authoring surface, `try_cvar_from_text` as the
validator, and `cvars_applied_by_map` reverting it all on unload. That whole
mechanism exists and is tested. Nothing new is needed for per-map modes; the
mode is one more line in the block.

The split to hold: **the mode row says which combinations are legal, the cvars
say how long and how many.** A number that could be any value is a cvar. A
choice between named behaviors is a row in the table.

## The gates doctrine, extended

`game_rules_system.hpp` already states the rule that makes mid-round joiners
correct for free:

> These are GATES, not effects: a caller asks every tick and acts on the
> answer, rather than something being applied to each player at the moment the
> phase changed. [...] Anything that CAN be expressed as a gate should be.

The value table is the same doctrine one level up. A gate is a pure query over
`(phase, settings)` — data in, bool out — so every mode difference expressed as
a flag inherits the mid-round-joiner property automatically. A mode difference
expressed as a stored per-player effect does not, and that is a second reason
to prefer the noun over the verb whenever both are available.

## The escalation trigger

Name it now so it is recognised rather than argued about later.

**The value table dies the day a mode needs state that no other mode has.** A
planted bomb and its timer. A flag carrier. Buy-time remaining. A row of
settings has nowhere to put any of that, and the temptation at that moment will
be to bolt the fields onto `game_rules_state_t` where every other mode leaves
them zero — which is the `Light_Entity` mistake exactly: an enum that selects
which fields are live is a type, not an enum.

The answer when it comes is a `std::variant<deathmatch_state_t, rounds_state_t>`
on `game_rules_state_t` plus a switch. One object, closed set, exhaustive
dispatch — the same argument the entity system already settled, and still not
virtuals and still not a VM.

The *other* escalation, the one that would justify a scripting layer, is a
person who needs to author a mode and cannot recompile. If that ever happens
the answer is embedding Lua or AngelScript over a stable C API. It is not a
bytecode format of this project's own.

## The line

**Values are data. Behavior is a named symbol.**

Every family in this codebase already holds it. Entities are structs plus
exhaustive switches. Assets are a generated manifest plus loaders that are link
errors until written. Both event channels are declarations plus a handler whose
absence is a link error. Modes are a table of settings plus two switches.

The moment the *behavior* starts becoming data — a condition tree, an
expression evaluator, a win-condition DSL — a VM is being written in
installments, and it will arrive with the worst 40% of one.

## Build order

Deathmatch first and completely, then Rounds. Not both at once: the second mode
is what proves the seams are in the right places, and it can only prove that if
the first one was built without knowing where they were.

1. `Game_Mode`, `Win_Condition`, `Spawn_Policy`, `game_mode_settings_t`, the
   table, the `static_assert`. Deathmatch row only. A `Rounds` entry in the
   enum whose row is currently a lie is worse than a one-mode enum.
2. Wire the three existing gates. `can_take_damage` into the damage path,
   `is_round_live` into scoring, respawn suppressed outside Live. This is the
   step that turns the FSM from decorative to load-bearing, and it is
   independent of everything else here.
3. `check_win_condition` on `Frag_Limit`, calling the existing `end_round`.
4. Durations to cvars; `sv_gamemode` as a cvar; confirm a map can set it
   through the Map Cvars panel and that unload reverts it.
5. Then the `Rounds` row: `phase_chain`, `Team_Elimination`, `Team_Markers`,
   team assignment on join, round-start respawn in `enter_phase`.

Steps 1 through 4 are what a four-player deathmatch needs. Step 5 is a second
mode on seams that have already been proven.

### What actually landed (2026-08-25), and where it differs

Steps 1 through 4 are done. `server/game_mode.hpp` holds the enums, the row and
the table; the FSM, the win condition and the mode resolution are in
`server/systems/game_rules_system.{hpp,cpp}`. Four deliberate differences from
the sketch above:

- **The phase chain became a phase CYCLE.** `Span<const Round_Phase>
  phase_cycle` is the part that REPEATS, with Warmup and Game_Over left out of
  it: Warmup happens once before any round and Game_Over once after the last, so
  putting them in every mode's list would be two bookends restated per mode.
  `next_phase` is three cases and names no phase — outside the cycle goes to
  element 0, a non-final element goes to its successor, the final element loops
  back or reaches Game_Over on `max_rounds`. Deathmatch's cycle is the single
  element `{Live}`, which is the honest shape rather than a degenerate one.
  `end_round` advances along the cycle too, instead of hardcoding `Round_End` —
  a deathmatch has no such phase and the old spelling would have put it in one.
- **`max_rounds` moved off `game_rules_state_t` onto the mode row.** It is a
  property of the mode, not of the match in progress, and having it on the state
  meant a mode could not declare it.
- **`Spawn_Policy`, `auto_assign_teams` and `join_in_progress` are NOT
  declared.** A one-value enum that nothing branches on is a field, not a
  decision, and `Team_Allegiance` is still read by nothing. They land with the
  Rounds row, listed at the bottom of `game_mode.hpp`.
- **`sv_gamemode` was a `string<24>`, not an enum cvar.** Enum-typed cvars were
  genuinely unimplemented in `def_gen`, so the text was resolved to a
  `Game_Mode` once, at map load, by a hand-written lookup against a `name`
  column on the row. **Paid off 2026-08-26** — see the step 5 section below.

One behavioral fix fell out while checking the deathmatch flow end to end:
`mp_warmup_seconds` defaults to **0**, meaning warmup has no deadline and ends
only when `start_match` decides enough players have connected. At the old
hardcoded 5 seconds the match reliably began on an empty server before the first
player finished loading, which made the round-start spawn snap land on nobody.

### Step 5 and the three cleanups (2026-08-26)

Everything above is now landed, and the two open cleanups with it. In the order
they were built, because each one is the next one's floor:

**Enum cvars, and `Game_Mode` moving into `cvars.def`.** `def_gen` grew
`CVAR_TYPE_ENUM`: a cvar may now be typed by an enum declared in the same
`.def`, its info row carries the family-neutral `enum_type_info_t*` that
`reflection.hpp` already defines for entity and event fields, and the text
conversion converts by VALUE NAME in both directions. A number is refused —
`sv_gamemode 0` would name a mode by index, which survives exactly until a value
is inserted ahead of it.

The knock-on is the interesting half. `Game_Mode` is declared in `cvars.def`
now, because that is the file `sv_gamemode` is typed from; `game_mode.hpp`
aliases it (`using Game_Mode = cvars::Game_Mode`) and holds only the behavior
each name selects. That **deleted** the row's `name` column and
`try_game_mode_from_name` — a second parser for something the generator already
knew — and turned `apply_game_mode_cvar` from a parse with an error path into a
one-line latch. The two halves cannot drift: a name with no row fails
`rows_in_enum_order`.

Note the ORDER that latch depends on, which was written down backwards and is
now right: `reset_game_rules` assigns `rules = {}`, so it runs FIRST and
`apply_game_mode_cvar` after it. The header said the opposite.

**Game_Over reloads the map.** `mp_game_over_seconds` (default 10) gives the
terminal phase a deadline, and it is the one deadline that names no transition:
it sets `game_rules_state_t::map_restart_requested`, which
`service_pending_map_restart` pays at the TOP of the next tick. A REQUEST rather
than a call, because `change_map_to` frees the session, the physics world and
every bot in it — servicing it where the FSM raises it would pull all of that
out from under the tick that raised it. 0 holds the final scoreboard forever,
which is what a server waiting on a map vote wants.

A reload rather than a bespoke "reset the match": it already resets the rules,
the scores (the players come back as fresh entities), the map's cvars and every
client's delta baseline, and it is the same path a rotation would call.

**The Rounds row**, and with it every field the sketch listed:
`Win_Condition::Team_Elimination`, `Spawn_Policy` (`Rotate_Markers` /
`Team_Markers`), `auto_assign_teams` and `join_in_progress`. `Team_Allegiance`
finally has readers on both the entities it was declared on: the win condition
counts bodies by team, `try_pick_human_spawn` matches the marker's, and
`spawn_bot` takes the bot's from the marker it spawned at. Three details worth
keeping:

- **Team_Elimination refuses to fire on two shapes**, and both have a test: both
  teams still alive (the obvious one) and only one team present at all. The
  second is the empty-server case — a naive "some team has no living player"
  test burns through every round of the match against nobody.
- **`join_in_progress` needed a new column**, `client_slot_t::wants_to_play`.
  `player_uid` cannot answer it: a player waiting out the round and a player who
  chose to spectate both have no body. It is the client's ANSWER rather than a
  fact about the world, so it survives a map load — which let `change_map_to`
  drop its `was_playing` array, a second copy of the same fact read one line
  before the reset that invalidated it.
- **The gate is the LIVE phase, not "a round exists"**: joining during warmup,
  the freeze or the settle spawns you immediately in every mode, because none of
  those is a round anyone can be reinforced in the middle of.

**And the gates stopped being synonyms.** `can_take_damage` was
`is_round_live` under another name, which meant warmup was a phase where bullets
passed through people — and with `mp_players_to_start` at 2, one player plus a
console-spawned bot never left warmup, so the first thing a solo test did was
shoot someone and watch nothing happen. Damage now applies in Warmup as well as
Live; SCORING is what warmup must not do, and that question is `is_round_live`,
asked at the two sites in `damage.cpp` that award a frag. The respawn schedule
asks it too — outside a live round a corpse always comes back, whatever
`respawn_during_round` says, or an elimination mode's warmup fills up with
bodies.

That is the gates doctrine paying out rather than bending: the header already
said `is_round_live` was the "does this gameplay count" question. Answering it by
disabling damage conflated *this does not count* with *this does not happen*.
`mp_players_to_start` defaults to 1 alongside it, so a server starts playing when
someone shows up; bots deliberately do not count toward it, so no value there
would have fixed the solo case on its own.

**And a test.** `game_rules_test` (35 in the suite now) walks both cycles tick
by tick, the mode table's own invariants (a non-empty cycle, `max_rounds >= 1`,
no bookend phase inside a cycle), the restart request and both its cvar values,
both win conditions including the two Team_Elimination must not fire on, the
team balance and every `Spawn_Policy` path. It stands Jolt up, unlike
`server_context_test`, because a round boundary moves every player's kinematic
capsule.

## What this does not decide

- **Team score.** Per-player kills and deaths landed as `@Networked` fields on
  `Player_Entity` (and player names with them), so the scoreboard is a bound
  pass over `latest_player_entities` and a late joiner gets every score from
  their first full snapshot. The TEAM's score is still nothing: it wants to ride
  `Round_Phase_Changed`, since it only changes at a phase boundary, which is
  exactly when that event fires. Nothing shows a team on the scoreboard yet
  either.
- **Team assignment policy beyond "the smaller team".** Switching, locking,
  whether a player may choose. `auto_assign_teams` says only whether teams are
  assigned at all; `pick_team_for_new_player` is deliberately a query over the
  bodies that exist rather than a tally to keep in step.
- **What happens after Game_Over, other than restarting the same map.** A
  rotation, a vote, or a return to Warmup without a reload. The restart is the
  honest floor: the match ends, and the server is playable again without an
  operator.
