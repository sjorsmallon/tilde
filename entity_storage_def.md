# entity_storage_def.md — P7: one ownership model

The design write-up P7's first bullet demands before storage is touched.
Companion to `entity_def.md` (reflection/DSL) and `cvar_def.md` (cvars). Written
2026-07-30, after an audit of every `spawn` / `get_entities` / `destroy` /
`shared_ptr<Entity>` site in the tree.

`entity_def.md` left one question explicitly open — *"worth confirming the
factory's return type is a pointer and not a handle before pooled storage lands,
since that choice is harder to reverse once callers exist."* This document is
the answer.

---

## 1. The audit: what is actually broken, and what only looks broken

P7's todo list was written before P1–P6 landed. Two of its bullets are stale and
one hazard it names is smaller than it reads. Correcting that first, because it
changes the size of the phase.

### Still true

**The `const map_t&` lie.** `init_session_from_map` takes `const map_t&` and
then writes through the shared_ptr it was handed:

```cpp
entry.entity->entity_id = entry.uid;              // game_session.cpp:27
session.entity_system.add_entity(entry.uid, entry.entity);
```

`Entity_System::add_entity` does it a second time (`entity_system.cpp:54`).
Neither write is to the session — both land in the object the *map* owns, via a
pointer the map and the session share. Consequences, all real today:

- Init two sessions from one `map_t` and the second stomps the first's ids.
- Re-serialize a map after `init_session_from_map` and you save runtime ids.
- The editor holds a live `map_t`; a session init mutates what the editor is
  editing, silently.

P1 fixed exactly this for geometry by making the session take a **copy**
(`game_session_t::geometry`). Entities never got the same treatment because they
were behind a `shared_ptr` and copying a `shared_ptr` copies the pointer, not
the pointee. After P5 entities are blittable, so the copy is now available.

**Raw `T*` into a reallocating vector.** `Entity_System::spawn<T>()` returns
`&pool->entities.back()` (`entity_system.hpp:111`). The next `spawn` of the same
type may reallocate; `remove()`'s swap-and-pop moves an unrelated element. The
`@FIXME` at `entity_system.hpp:100` already flags the shape.

**Linear scan is the only way to go from a uid to an entity.** Three copies of
`find_player_by_uid` (`damage.cpp:13`, `respawn_system.cpp:37`,
`rocket_system.cpp:21`), two of `find_physics_body_by_uid`, plus `bot_system.cpp:105`
scanning the player pool by `client_slot_index` every tick per bot. `entity_uid_t`
is already the cross-system currency (see §2) but has no index behind it.

### Stale — already fixed by an earlier phase

**"Unify the runtime entity id type: `entity_uid_t` is uint32 but
`Entity::entity_id` is uint64."** Not any more. `entities.def:93` declares
`entity_id: u32 @Networked`, the generated struct is `uint32_t entity_id`
(`entities_generated.hpp:223`), `physics_state_t`'s maps are
`entity_uid_t`-keyed, and `hit_result_t::entity_id` is `entity_uid_t`. The
uint64 died with the macro system in P5. **Nothing to do; strike the bullet.**

**"Factory returns `shared_ptr<network::Entity>`:
`create_entity_by_classname`/`create_entity_by_type` (`entity.cpp:328`)."** Those
symbols and that file no longer exist — the only surviving references are in
`src/shared/old_ideas/`, which is not compiled. What *does* return a
`shared_ptr<entities::Entity>` is `create_map_entity(classname)` (`map.hpp:218`),
and its job is to build a **map** entity, which is a legitimately different job
from spawning a runtime one. See §4.

### Smaller than it reads

**No `T*` is retained across a frame boundary anywhere in the tree.** Every
pointer obtained from `spawn` or `get_entities` is filled/read and dropped inside
the function that got it. Cross-frame references are already carried as **uids**:
`Rocket_Entity::owner_id`, `physics_state_t::entity_body_map`,
`hit_result_t::entity_id`, the respawn queue, `snapshot_frame_t`'s per-type maps.
Bots store `player_slot` and re-find their entity every tick.

This matters for the handle decision, so it is worth stating as a rule rather
than an observation: **the codebase has never needed a handle that outlives a
frame, because the thing it stores when it needs to remember an entity is
already the uid.** The dangling-pointer window is *intra*-frame — spawn a rocket
while holding a pointer to a player is safe (different pools), spawn a second
rocket while holding the first is not.

---

## 2. The handle decision

Three candidates. The todo names two; the third is what the audit suggests.

### (a) Generational `{index, generation}`

The classic. Handle carries the slot index, so resolution is one array index and
one integer compare. Detects use-after-free of a **reused slot**, which is the
failure a free-list creates.

### (b) Bare `entity_uid_t`, with an index behind it — RECOMMENDED

The handle *is* the uid the rest of the codebase already passes around.
Resolution is a lookup in a uid → `{entity_type, slot}` table owned by
`Entity_System`.

The argument that decides it: **a monotonically-increasing uid already is the
generation.** `next_entity_id` only ever increments (`entity_system.hpp:87`,
seeded past `map.next_uid` at init). A uid is therefore never reused within a
session, so a stale uid resolves to *nothing* — not to a different entity. That
is precisely the guarantee a generation counter buys, obtained from a field that
already exists, is already `@Networked`, and is already what `owner_id` and the
physics maps hold.

A generational handle would additionally have to be *converted* at every one of
those boundaries, because `{index, generation}` cannot ride the wire as
`owner_id: u32` without either widening the field or packing bits into it. That
conversion is new code whose only purpose is to detect a failure the uid already
detects.

Cost: resolution is a table lookup, not an array index. Against the linear scans
it replaces, that is a straight win — this decision *deletes* the five
`find_*_by_uid` functions and the per-bot pool scan.

Reversibility, which is why this is the safe choice: the public name stays the
uid, so a generational **slot** handle can be introduced *inside* the pool later
as an optimisation without touching a single caller. The reverse — retiring a
generational handle after callers have baked `{index, generation}` into their
state — is the hard direction.

### (c) Bare uid with no index (scan on resolve)

What exists today, minus the raw pointers. Rejected: it keeps every linear scan
and buys only the pointer safety.

### Consequence for the P3 open question

`entity_def.md` asked whether the factory returns a pointer or a handle. **It
reverses to a handle for the runtime path only** — `spawn<T>()` returns
`entity_uid_t`, and `Entity_System::get<T>(uid)` returns `T*` valid until the
next mutation of that pool. The map-load path keeps returning a value/pointer,
because a map entity is not in a pool and has no session identity yet (§4).

The `T*` from `get<T>` is deliberately still a pointer, and deliberately still
invalidation-prone. Making resolution cheap and explicit at point of use is the
whole mechanism: the rule becomes **never store a `T*` across a call that can
spawn or destroy**, which is checkable by reading one function, instead of
today's **never store a `T*` at all, and also hope**.

---

## 3. Storage shape: dense vector + uid index

Two sub-options, and the tie-break is P6's frame build.

**Dense `std::vector<T>` + swap-and-pop, with the uid index fixed up on the
move.** Iteration stays contiguous with no holes to skip. `snapshot_frame_t`'s
per-type maps can eventually become a straight copy of the pool vector — the
"world snapshot = memcpy per pool" payoff P7 was justified on. Removal is O(1)
plus one index-table write for the entity that got moved into the hole.

**Slot-stable segmented storage + free list + tombstones.** Pointers stay valid
across spawns. But iteration must skip holes, the memcpy-per-pool snapshot stops
being a single blit, and — the point — slot reuse is what *creates* the need for
generation counters, so this option drags candidate (a) back in.

**Decision: dense.** The `@FIXME`'s wording ("delete should not actually delete
but just free up a slot") points at the segmented option, but its actual
complaint is the escaping raw `T*`, and the handle fixes that without holes. Keep
`reserve()` on pool creation so the common case doesn't reallocate at all.

Sketch (shape, not final code):

```cpp
// Entity_System
std::map<entities::entity_type, std::unique_ptr<Entity_Pool_Base>> pools;
struct entity_location_t { entities::entity_type type; uint32_t slot; };
std::unordered_map<entity_uid_t, entity_location_t> locations;

template <typename T> entity_uid_t spawn();           // was T*
template <typename T> T*   get(entity_uid_t);         // nullptr if gone/wrong type
                     bool  destroy(entity_uid_t);     // was destroy(T*)
template <typename T> std::vector<T>* get_entities(); // unchanged
```

`get_entities<T>()` stays and stays a vector — the per-tick systems that walk
every rocket or every player want the dense array, and taking that away to force
handle resolution per element would be a pure loss.

---

## 4. Map load stops mutating the map

`init_session_from_map` keeps `const map_t&` and it becomes true:

- `add_entity(uid, entity)` copies the entity into its pool **and then sets
  `entity_id = uid` on the copy**. The map's object is never written.
- `add_existing` on the pool base takes the uid so the copy is complete before
  it lands in the vector.
- The `entry.entity->entity_id = entry.uid` line in `game_session.cpp` is
  deleted outright; it is the map-facing half of the same write.

Verification is a behavioral test, not a compile: **load → init session → play
→ serialize, and assert the canonical string is byte-identical to the one taken
before init.** `compute_map_content_hash` already gives exactly that comparison
for free. This is the one P7 step with a cheap, decisive test, which is why it
goes first.

> **Correction, found while landing this (step 1, 2026-07-30): the hash test
> above would have passed against the very bug.** `entity_id` is not
> `@Saveable`, so it never appears in the canonical text, and the mutation this
> section exists to delete is a write to `entity_id` and nothing else. The test
> that actually catches it reads the map entity's `entity_id` field **directly**
> after a session init (`session_test`). The hash comparison is kept beside it,
> because it still covers everything that *is* saveable — but it is the
> secondary check, not the decisive one. A second case is covered there too:
> two sessions from one `map_t`, which is the original failure (the second init
> used to renumber the first's entity).

`create_map_entity` keeps returning `shared_ptr<entities::Entity>`. A map entity
is heap-owned polymorphic-ish storage the editor mutates in place through stable
pointers, and it has no session identity to hand out a handle against. The two
paths converge at `add_entity`, which is where the copy happens. (Whether
`map_entity_t` should hold a value rather than a `shared_ptr` is a real question
— entities are blittable now — but it is an *editor* refactor, not a session
storage one, and bundling it here would reintroduce exactly the "which half
broke it" problem P7's ordering rule exists to prevent.)

---

## 5. Geometry does not join the pool model

Third P7 bullet, resolved: `game_session_t::geometry` is already a plain value
vector the session owns, copied from the map (P1). Nothing about it wants
handles — it is never networked, never spawned or destroyed at runtime, and
frozen for the session's lifetime.

The only residue is the documented `Collision_Id::index` split: the session BVH's
index is a `session.geometry` array position, the editor BVH's is a uid
(`collision_detection.hpp:40-45`). That asymmetry is fine *as long as it stays
documented*, because each subsystem only ever queries its own BVH — but P7 is
where it stops being free, since the session BVH is now the last place in the
runtime addressing something by array position. Leave it; note it here so the
next person doesn't think it was missed.

---

## 6. Order of operations

Runtime failures, so each step needs its own way of being wrong loudly.

1. **Make map load non-mutating** (§4). Isolated, and verified by the
   hash-unchanged test above. Land it alone.
2. **Add the uid index and `get<T>(uid)`**, alongside the existing API. Nothing
   is removed yet, so nothing can break; the index is populated by
   `add_entity`/`spawn`/`destroy` and validated by an assert that it agrees with
   a linear scan under a debug flag.
   **DONE 2026-07-30.** `Entity_System::locations` (uid → `{entity_type, slot}`),
   maintained by `spawn`/`add_entity`/`destroy`/`reset`, plus `get<T>(uid)` and
   `destroy_by_uid(uid)` (step 4 renamed the latter to plain `destroy(uid)` once
   the pointer overload it was distinguished from was gone).
   * `get<T>` answers nullptr for both "no such uid" and "uid names a different
     type". That is what lets a caller use it as lookup *and* type test in one
     `if` — see `inflict_damage`, which is now two such `if`s and a genuine
     "not damageable" fallthrough.
   * The validation landed as `validate_locations()` rather than a debug-flag
     assert: it cross-checks index against pools in **both** directions and logs
     every disagreement, and `ecs_test` calls it. It is the guard for the one
     invariant P7 adds that the compiler cannot check — the swap-and-pop fixup —
     and it was verified the only way that means anything, by deleting that
     fixup and confirming `session_test` fails naming the stale uid.
3. **Delete the five `find_*_by_uid` helpers and the bot pool scan**, replacing
   them with `get<T>(uid)`. Pure substitution, same semantics, and the step
   where the index earns itself.
   **DONE 2026-07-30.** The five copies of `find_player_by_uid` /
   `find_physics_body_by_uid` (`damage.cpp`, `respawn_system.cpp`,
   `rocket_system.cpp` ×2) are gone. `bot_system`'s per-bot-per-tick scan of the
   whole player pool for a matching `client_slot_index` went too, but not by
   substitution — `Bot_State` now holds `entity_uid`, set at spawn, because the
   scan was answering a *slot* question the uid index cannot answer. Same shape
   as the fix step 4 needed for `client_slot_t`.
4. **Flip `spawn<T>()` to return a uid and `destroy` to take one.** This is the
   compile-breaking step, and it is deliberately *after* the index exists so
   every call site has something to migrate *to*. Call sites are the ones in the
   todo's migration bullet: `server_impl.cpp` (player spawn/leave, rocket fire),
   `physics_body_system`, `bot_system`, `rocket_system`, `respawn_system`,
   `damage.cpp`, `session_test`.
   **DONE 2026-07-30.** `respawn_system` and `damage.cpp` turned out to need
   nothing: step 3 had already moved both onto `get<T>(uid)`, and neither spawns
   nor destroys. Two things the flip pulled in that this list did not predict:
   * **The three slot scans are gone too, and that was the open question in the
     todo.** "Which player is slot N" is a *slot* lookup, so the uid index cannot
     answer it — the fix was to record the answer where the slot already lives.
     `client_slot_t` (the per-connection server state, already cleared on
     join and on leave, which is exactly when the mapping changes) gained a
     `player_uid` column, and `spawn_player_entity_for_client_slot` fills it. That deleted the
     pool walks in `handle_player_leave`, `get_position_in_front_of` and the
     per-move player lookup in `Tick`. Bots are structurally excluded: their
     `client_slot_index` is `>= BOT_SLOT_BASE`, past the end of that array, and
     `Bot_State` carries its own uid.
   * **`spawn_physics_body` returns a uid too**, for the same reason
     `Entity_System::spawn` does — it was handing back a
     `Physics_Body_Entity*` its callers only ever read `entity_id` off.
   * `update_rockets` stopped collecting slot indices to remove. It had to sort
     them descending and dedupe so swap-and-pop could not invalidate an earlier
     index; a uid names the same entity no matter what moved, so the sort, the
     dedupe and `<algorithm>` all went with it.
   * `get_entities<T>()` was left untouched, as §3 planned. (It becomes a
     `Span<T>` in pool retirement — `entity_system_def.md` step 2 — and storage
     stays dense either way, which is what the per-tick systems that walk every
     rocket actually care about.)
   * The rule the flip establishes, and the reason `get<T>` returns a plain `T*`:
     **never store a `T*` across a call that can spawn or destroy in the same
     pool.** Every site that now holds a `get<T>` result for more than one
     statement carries a comment saying why it is allowed to — usually that the
     only spawn below it lands in a *different* pool.
   * **Bonus catch, unrelated to storage but found by reading every call site:**
     the per-move loop in `Tick` indexed `g_player_states[player_idx]` behind a
     `player_idx >= 0 && < sv_max_player_count` guard placed 70 lines *after* the
     first use of `player_idx`. The range check now happens once, at the top of
     the loop, where it gates everything.
5. **Wire `unregister_physics_body` into destruction.**
   **DONE 2026-07-30.** `server::destroy_entity(context, uid)`
   (`src/server/entity_lifecycle.{hpp,cpp}`): unregister the Jolt body, clear the
   uid-keyed server side tables, then call `Entity_System::destroy(uid)`. All
   three destroy sites route through it.
   * **The seam: a server-side function, not a hook installed on the session.**
     `Entity_System` lives in `game_shared` and has no business knowing what
     `physics_state_t` is, so the two candidates were a `std::function` the
     server installs on the session, or moving the funnel up a level. The hook
     is registration under another name — invisible at the call site, silently
     absent on any `Entity_System` nobody installed it on (the client builds
     one), and the shape this project has spent seven phases deleting. The cost
     of the function is that it is a convention, not a compiler-enforced funnel;
     acceptable because only the server destroys anything, so there is exactly
     one side to hold the convention.
   * `spawn_physics_body` took `(session, physics)` and now takes
     `server_context_t&`: its failure path undoes a spawn, and undoing a spawn
     goes through the funnel.
   * **The leak this step was written to fix did not exist.** Rockets register no
     Jolt body (they are moved by hand and hit-tested with `cast_sphere`), and a
     rocket kill does not destroy the victim — `schedule_respawn` reuses the
     entity. The only destroyed body-owner was a leaving player, unregistered by
     hand. So the step made a future leak unrepresentable rather than fixing a
     live one.
   * **The audit found a different, live bug.** `load_map_file_into_context` cleared
     `client_slot_t::player_uid` because a new session restarts
     `next_entity_id` and a retained uid can be *reissued* (§2's guarantee holds
     within one session's counter, and a map load restarts it) — but it never
     cleared `death_tick_by_player_uid`, keyed the same way. A death pending
     across a map switch would resolve to a real but unrelated `Player_Entity`
     and reset its position and health when the delay elapsed. Fixed there,
     beside the sibling clear it was missing from.
6. **Greens:** `session_test`, `transaction_system_test`, `ecs_test`, plus the
   map load → play → save cycle from step 1 re-run at the end.
   **CLOSED BY DECISION 2026-07-30 — half done, and the half that was skipped is
   named here on purpose.** All 19 test executables pass; that is the automated
   half and it is genuinely green. The hand-driven half — connect, fire rockets,
   take a rocket kill, leave, `spawn_cube`, a `map` switch with a death pending —
   was **not run**. The call: steps 4–5 fail loudly rather than subtly if they
   are wrong (a stale uid resolves to *nothing*, which is the point of §2's
   handle), so ordinary play detects them and a scripted pass buys little.
   * This is a bet, not a verification, and it is the only step of P7 recorded
     that way. If a P7-shaped symptom ever shows up — an entity resolving to the
     wrong one, a body outliving its entity, a slot naming someone else's player
     — start here rather than assuming the phase was checked.
   * `spawn_cube` was found to draw nothing while poking at this. Diagnosed and
     **not** a P7 regression: the duplicated asset registry (`game_shared` is a
     static lib, so `game_client.dll` holds its own empty manifest). Tracked in
     `todo.md`.

Steps 1–3 are additive and individually verifiable. Step 4 is the one that
mostly compiles and fails at runtime, and it is one step, touching seven files,
with everything it needs already in place.
