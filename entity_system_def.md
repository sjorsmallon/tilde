# entity_system_def.md — retiring the pre-generator pool

`Entity_System` is older than the DSL. Its shape answers a question the
generator has since answered, and this document is the removal plan.
Companion to `entity_def.md` (reflection/DSL), `entity_storage_def.md` (P7
ownership) and `cvar_def.md` (cvars). Written 2026-07-30.

This document does **not** re-specify P7 steps 4–5. Those are scoped in
`entity_storage_def.md` §6; §5 below only says where they sit relative to the
storage swap. **Status: all of P7 landed 2026-07-30** (step 4 did not land
unchanged — it also gave the server's per-slot state a `player_uid` column and
flipped `spawn_physics_body`; both recorded there, neither affecting this phase.
Step 6 closed by *decision* rather than by a runtime pass, which §5 step 1 flags
as the corner to check if the storage swap ever exposes something odd).

**Status: this phase is COMPLETE as of 2026-07-30.** All four steps landed, the
tree builds and all 19 tests pass. The work list moved out of `todo.md` into
`done.md` when it closed; this file stays the design and the detailed log. `Entity_Pool_Base`, `Entity_Pool<T>`,
`make_entity_pool`, `register_all_known_entity_types` and the `std::map` of
`unique_ptr`s are gone; storage is a `std::array<Entity_Pool, ENTITY_TYPE_COUNT>`
over byte buffers. What is written below in the future tense is kept as the
record of the reasoning, with the outcome noted per step in §5. Next up is P8
(protobuf removal), which shares no code with this.

---

## 1. The question that no longer exists

Every structural choice in `entity_system.{hpp,cpp}` descends from one need:
*get from a runtime `entity_type` to a compile-time `T`.* Pre-generator that
cost a virtual interface (`Entity_Pool_Base`), heap-allocated pools behind
`unique_ptr`, a `std::map` keyed by tag, and a hand-maintained switch
(`make_entity_pool`) that had to name all eight types.

The generated table already answers it, as data:

```cpp
struct entity_type_info_t {
  uint32_t size_in_bytes;
  uint32_t alignment;
  Entity* (*construct_at)(void* memory);   // "type-erased hook for callers
  ...                                      //  that already own their storage:
};                                         //  ... pooled storage"
```

`construct_at`'s own doc comment names pooled storage as a client. The pool is
the one caller on that list that never took the hook.

`Entity_Pool<T>` uses `T` for exactly four things — `sizeof`, default
construction, copy-assignment, and `entity_as<T>`'s tag compare. The first three
are columns in that table; the fourth is a comparison against
`Entity::type`, which every entity carries. **Nothing is left that needs a
template parameter**, and once the pool has none, the switch that existed to
supply one has no reason to exist either.

**Decision: the pool becomes a plain struct and the entity type becomes a
field.** Storage stays dense — `entity_storage_def.md` §3 settled dense vs.
slot-stable and nothing here reopens it. (`Stable_Array` in `shared/array.hpp`
was re-examined and declined: it is itself a template on `Type`, so it would
leave the switch exactly where it is, and its compile-time `Slot` sizing and
address-masking make it the harder of the two to type-erase.)

---

## 2. The shape

```cpp
// One pool per entity type. NOT a template: the generated table carries
// everything the storage needs to know about the type, so the type is a field
// here rather than a parameter.
//
// Dense and stride-packed. Removal is swap-and-pop, and the uid index owns the
// fixup (entity_storage_def.md §3).
struct Entity_Pool
{
  entities::entity_type  type   = entities::entity_type::Invalid;
  uint32_t               stride = 0;   // entity_info(type).size_in_bytes, cached
  uint32_t               count  = 0;
  std::vector<std::byte> storage;

  entities::Entity*       at(uint32_t slot);
  const entities::Entity* at(uint32_t slot) const;

  entities::Entity* push_default();                     // construct_at a fresh slot
  entities::Entity* push_copy(const entities::Entity*); // asserts the tag, then memcpy
  pool_removal_t    remove_at(uint32_t slot);           // swap-and-pop
};

struct Entity_System
{
  // Indexed by (uint32_t)entity_type; index 0 is Invalid and stays empty. An
  // array of values cannot be missing an entry, so "no pool for type X" stops
  // being a state this code can be in -- and there is nothing to register.
  std::array<Entity_Pool, entities::ENTITY_TYPE_COUNT> pools;
  std::unordered_map<entity_uid_t, entity_location_t>  locations;
  entity_uid_t next_entity_id = 1;

  Entity_System();  // sizes each pool from entity_info(type). No dispatch inside.

  // Typed. T comes from the CALL SITE, which already knows it -- which is why
  // no tag-to-type dispatch is needed anywhere.
  template <typename T> Span<T>      entities_of();
  template <typename T> T*           get(entity_uid_t uid);
  template <typename T> entity_uid_t spawn();          // was T*  (P7 step 4)

  // Untyped. Never needs T at all.
  entity_uid_t add_entity(entity_uid_t uid, const entities::Entity* source);
  bool         destroy(entity_uid_t uid);
  void         reset();
  void         populate_from_map(const map_t& map);
  bool         validate_locations() const;
};
```

`entities_of<T>()` resolves as `pools[(uint32_t)T::static_type]` and hands back
`Span<T>` over the bytes. Returning a span rather than the container is what
makes §5 step 3 (the storage swap) a one-file change — and it also narrows the
callers, who can
currently `push_back` into a pool behind `locations`' back through the
`std::vector<T>*` they are given.

---

## 3. What gets deleted

| Gone | Why it existed |
|---|---|
| `Entity_Pool_Base` and its 5 virtuals | a common handle for `Entity_Pool<T>` |
| `Entity_Pool<T>`, the template | `sizeof(T)` / construct / copy — table columns now |
| `make_entity_pool` and its switch | the tag-to-type bridge |
| `std::map` + 9 `unique_ptr`s + 9 allocations | there was no `ENTITY_TYPE_COUNT` pre-generator |
| `register_all_known_entity_types` + the `@NOTE(SJM)` constructor apology | the hazard it defends against becomes unrepresentable |
| **4 ×** "no pool for entity_type X" branches + their null checks | a map lookup can fail; an array index cannot |
| ~~`destroy(T*)` and its pointer-range check~~ **already gone** | superseded by uid handles — deleted by P7 step 4, 2026-07-30 |
| `uid_at(slot)` on the base | `at(slot)->entity_id` |
| `invalid_entity_slot` as a return channel | `push_copy` asserts rather than returning a sentinel |

About half of `entity_system.cpp` is error handling for states an array of values
cannot reach. `validate_locations` loses its whole pool-existence half and keeps
the half that is actually interesting: index versus pool agreement.

`make_entity_pool` also drops off the sanctioned-exhaustive-switch lists in
`CLAUDE.md` and `src/shared/entities/README.md`. The remaining four switches
stay — they dispatch *behaviour*, which is the case where a switch earns itself.

---

## 4. The two invariants this rests on

**Entities are trivially copyable and trivially destructible.** `push_copy` and
`remove_at` are `memcpy`, and the byte pool never runs a destructor. True today —
every field is a scalar, a `vec3f`, an enum, an inline `pascal_string_t`, or a
component of those. The old guard was four *hand-written* asserts in
`entity_layout_test.cpp:12-15`, covering four of the eight types — a
hand-maintained per-type list, which is the exact shape §1 exists to delete. So
step 1 has `def_gen` emit, per entity: (**in the tree since 2026-07-30** — the
hand-written four are gone)

```cpp
static_assert(std::is_trivially_copyable_v<Player_Entity>);
static_assert(std::is_trivially_destructible_v<Player_Entity>);
static_assert(std::is_base_of_v<entities::Entity, Player_Entity>);
```

These are load-bearing, not decoration: the day a field arrives that breaks the
first one, the byte pool leaks silently and nothing else would say so. That was
checked rather than assumed when they landed — a `std::string` injected into
`Player_Entity` fails the copyable and the destructible assert, each naming the
type and why it matters. An assert nobody has watched fire is a comment.

**Reaching the base does not assume a layout.** `at(slot)` needs an `Entity*`
from a `std::byte*`. Do *not* bet on `Entity` sitting at offset 0: `Entity` has
data members and so does every derived type, so the types are not
pointer-interconvertible and `offsetof` past the base is not something the
standard hands over. There is no honest `static_assert` for it.

Instead, **generate the upcast**, exactly as `construct_at` already does — one
more column:

```cpp
Entity* (*as_base)(void* memory);   // return static_cast<Entity*>((Player_Entity*)memory);
```

The generator emits it in a TU where the type is complete, so the compiler
performs whatever adjustment the ABI wants and the pool makes no layout
assumption at all. Cost is one indirect call on the cold paths (`add_entity`,
`remove_at`, `validate_locations`); the per-tick paths go through
`entities_of<T>()` and never touch it.

Because no `static_assert` can vouch for this one, `entity_layout_test` checks it
at runtime instead: `as_base` must hand back the *same pointer the language
produces* from a typed `Rocket_Entity*`, and every type must carry both hooks —
the pool indexes the table by tag, so a hole would be a null call rather than a
lookup failure.

One caveat that does remain, stated so it is a decision and not an accident:
`entities_of<T>()` reinterprets the buffer as `T` and strides across elements
that do not form an array object. Formally UB, universally fine, and the same bet
the codebase already makes when it memcmp-diffs and memcpy-clones entities.

---

## 5. Order of operations

**This phase runs after P7 completes, with one deliberate exception.** An earlier
draft of this section put the storage swap *before* P7 step 4, justified by "then
step 4 is written once against final storage." That justification was wrong:
P7 step 4 changes `spawn`'s signature and its 11 call sites, the storage swap
changes `spawn`'s *body*, and the two do not overlap at a single site. Nothing is
migrated twice either way — so the argument evaporates and `todo.md`'s ordering
rules decide instead. Both would touch `server_impl.cpp` and the server systems
concurrently, which is exactly the "which half broke it" hazard P7's rule exists
to prevent.

1. **Step 1 — `def_gen` emits the three asserts from §4 and the `as_base`
   column.** Additive, behaviour-free, and nothing calls the column yet. A green
   build here is the proof that §4's invariants hold and therefore that the rest
   of this phase is legal at all — far cheaper to learn here than during step 3.

   **DONE 2026-07-30, and green.** 24 asserts (8 types × 3) plus 8 `as_base`
   thunks; the whole tree compiles and all 19 test executables pass. That is the
   proof §4 asked for, so step 3 is legal. Three notes worth keeping:
   * The hand-written stand-in asserts in `entity_layout_test.cpp` are deleted —
     including the generated header IS the check now. What stayed in that test is
     the half no assert can express: the runtime `as_base` agreement above.
   * **`SCHEMA_HASH` is unchanged.** The hash is mixed from the parsed `.def`,
     not from the emitted text, so a build with these asserts still connects to
     one without them. Expected, but confirmed rather than assumed — a
     table-shape change looks exactly like the thing the handshake exists to
     catch.
   * The step was written to land *ahead of P7 step 4*, on the reasoning that it
     is a **reflection** change and `todo.md`'s first ordering rule forbids those
     riding along with P7's storage steps — leaving "before P7 step 4 or after
     P7" as the only legal slots. P7 step 4 landed first and this did not, so it
     took the second slot. Not a lost opportunity, just a smaller one: the proof
     arrives before the storage swap either way, which is the only place it is
     load-bearing.

   *(P7 is done — `entity_storage_def.md` §6 is its log. Note its step 6 closed
   by decision rather than by a runtime pass, so if this phase's storage swap
   ever exposes something that looks like it predates it, that is the corner to
   check.)*

2. **Narrow the accessor, keep the storage.** `get_entities<T>()` becomes
   `Span<T> entities_of<T>()`, still backed by `std::vector<T>`. 15 call sites,
   no storage change.

   **DONE 2026-07-30, and green** — the whole tree compiles and all 19 test
   executables pass. Three things it taught, worth having before step 3:
   * **9 `if (!pool)` null checks disappeared.** An empty span answers both "no
     entities of this type" and "this type has no pool" — correct, since the
     second was a registration bug rather than a runtime state, and §3 deletes it
     outright.
   * **One site was not mechanical, and it is exactly the hazard this step
     exists to surface.** `server_impl.cpp`'s per-tick trigger walk fetched
     `player_pool`, and the snapshot frame build ~100 lines below reused it. With
     a `std::vector<T>*` that was safe — the pointer outlives a reallocation and
     only the ELEMENTS move — so the reuse had been quietly correct for as long as
     it existed. A span carries the data pointer AND the count, so the same reuse
     reads freed memory. It re-fetches now, beside the rocket and physics-body
     fetches already there. Latent rather than live (nothing between them spawns a
     player: `fire_trigger_action` → `inflict_damage` neither spawns nor
     destroys), but it is the difference between a rule callers are told and a
     rule the type enforces.
   * Two other long-range holds were audited and kept: `bot_system`'s `players`
     span and `server_impl`'s bot-spawn loop over the spawn-marker pool. Both hold
     across a spawn into a *different* pool, which cannot invalidate them; both
     now say so at the site.

3. **Swap the storage.** Everything in §3 is deleted and the byte pool lands.
   Because step 2 hid the container, this touches `entity_system.{hpp,cpp}` plus
   the one debug overlay that iterates `pools` directly (`play_state.cpp:1093` —
   it keeps working, since `pool.type` becomes a member rather than a map key).

   **DONE 2026-07-30, and green** — the whole tree compiles and all 19 tests
   pass. It came in at the predicted size: `entity_system.{hpp,cpp}` plus three
   one-line outside edits. Four things worth keeping:
   * **`add_entity` takes `const entities::Entity*`**, not the map's
     `shared_ptr`. What it needs is bytes to copy from, and taking the owning
     handle is what tied session storage to an editor allocation decision — §6's
     `map_entity_t` question is now free to move without touching this file.
     One call site (`game_session.cpp`) gained a `.get()`.
   * **The type-mismatch branch did not survive, and could not have.**
     `add_entity` selects the pool BY `entity->type`, so the mismatch
     `add_existing` used to report through `invalid_entity_slot` is not
     reachable from there. `push_copy` asserts on it as a backstop rather than
     as a path, which is what §3 meant by retiring the sentinel.
   * **One coupling is load-bearing and nothing was checking it**: the pool
     addresses elements by `entity_info(type).size_in_bytes` while
     `entities_of<T>()` walks them by `sizeof(T)`. They agree by construction —
     the generated column IS `(uint32_t)sizeof(T)`, confirmed by reading
     `ENTITY_INFOS` rather than assuming — but a disagreement would leave
     element 0 correct and every later element garbage, which is the worst
     available failure mode. `entities_of<T>()` now asserts it. **Watched fire**:
     stride deliberately skewed by 4, `session_test` aborts naming the entity
     and the reason. A second assert covers over-alignment, since the buffer
     comes from `operator new` and only guarantees fundamental alignment.
   * `client_impl.cpp:79`'s second `register_all_known_entity_types()` call went
     out with the function, as planned — no separate fix was needed.

4. **Update the prose.** `make_entity_pool` comes off the
   sanctioned-exhaustive-switch lists in `CLAUDE.md` and
   `src/shared/entities/README.md`.

   **DONE 2026-07-30.** Both lists now name four switches, and both say
   explicitly that **storage is not among them** — the useful half of the edit,
   since "adding an entity" is exactly when someone goes looking for the place
   to register it. `README.md`'s "there is no step where you register anything"
   is now true of storage too, rather than true of everything except storage.

The P7 interlude is deliberately *not* numbered here, so that these four numbers
mean the same thing as `todo.md`'s four. An earlier draft numbered it as item 2
and every "step N" cross-reference in this file was off by one against the work
list.

**Greens:** `session_test` calls `validate_locations` four times — around the
uid index, across a swap-and-pop removal, and after `reset()` — which is what
guards step 3 directly. Plus `entity_layout_test` (the `as_base` agreement §4
cannot express as a `static_assert`) and the map load → play → save hash
comparison from `entity_storage_def.md` §4, which `session_test` also carries.

*(An earlier draft credited `ecs_test` with the `validate_locations` call. It
has never had one: `ecs_test` exercises `shared/old_ideas/ecs.hpp`, an unrelated
component registry that `Entity_System` does not use and that this phase does
not touch. The guard was always `session_test`. Corrected 2026-07-30 —
`todo.md` inherited the same error and is fixed with it.)*

Since 2026-07-30 all 19 are one command — `ctest --test-dir cmake_build -j8`,
~2s — with `WORKING_DIRECTORY` pinned to the project root, which retires the
standing "must run from the project root" footgun rather than restating it.

**Independent of P8 (protobuf removal).** P8 converts message envelopes; this
converts session-internal storage. They share no code. The only apparent overlap
is `S2C_EntityPackage`, where the payload is already hand-rolled bitstream and P8
swaps only the envelope. This phase goes first because it is smaller and already
specified, not because P8 depends on it.

---

## 6. Adjacent, and deliberately not here

- **`map_entity_t::entity` is a `shared_ptr<entities::Entity>`**
  (`map.hpp:31-35`) — genuinely pre-generator: a heap allocation per map entity,
  and a base-pointer `shared_ptr` over a type with no virtual destructor (safe
  only because the deleter is captured at construction). The storage swap's
  `add_entity(uid, const Entity*)` decouples `Entity_System` from it, so this can
  be dealt with later without touching any of the above. `entity_storage_def.md`
  §4 already ruled it an editor refactor rather than a storage one.
- **`snapshot_frame_t`'s three hand-listed `unordered_map`s**
  (`entity_snapshot.hpp:89-91`) — a hand-maintained per-type list of exactly the
  kind §1 removes, and the natural next target. It belongs to the snapshot-delta
  work, not to this.
- **The session BVH's `Collision_Id::index`** — still an array position rather
  than a uid (`entity_storage_def.md` §5). Untouched; noted so it does not look
  missed.
