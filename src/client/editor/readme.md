# Editor Ramblings

> **⚠ Entities now come from `entities.def`, not a `SHARED_ENTITIES_LIST`
> X-macro.** See `entity_def.md` at the repo root. The "Entity Editor Traits"
> section below was rewritten (2026-07-31) once the generated `entity_type`
> enum made the old per-type template specialization mechanism redundant —
> see `entity_editor_traits.cpp` for the current shape.

I ran into issues with an imperative approach to my editor. the long if-else chain of 
```
if (a _pressed)
    if (shift_pressed)
    else if (control_pressed)
....
```
became very onerous and was not the solution. I then moved to a simple dispatch mechanism where we keep a table of function pointers and key maps to know which
combination of keys should dispatch to whatever button. however, the state management
and serialization with the map became very ad-hoc and I did not know how to generalize it properly.


After some digging and investigation, it seems that a "common"(?) pattern is to use a `tool` pattern. one tool for selection, one tool for sculpting, one tool for placement.

The problem here then is how to abstract those different tools in a meaningful way to support all functionality that is needed. This is not something I came up with but stole from somewhere else and defined in `editor_tool.hpp`. the gist of it is that the `tool_editor_state` keeps track of which tool is active and dispatches to the correct tool.

I ran into more issues with selection not being generalizable and also how to keep track of entities and static geometry etc. We had AABBS which are trivial, but I wanted to introduce wedges and then it becomes evident that keeping a linear list of all these shapes in the map is also not very fruitful. The current solution that I picked is that the map contains a `static_geometry_t` variant which is now either an AABB, a wedge or a `mesh` (not directly embedding the vertices, but pointing to an asset but having an up-to-date bounding `AABB`).

this also simplifies picking in the editor!

now the next problem to fix is the undo / redo stack. The previous solution uses a pair of function pointers that do undo / redo, but that to me looked like a very fragile mechanism relying on order of insertion etc. Additionally, it means implementing an undo / redo for every single unique action and that also does not seem to scale well IMO. I want to move to a transaction mechanism which can be easily undone or done.

## Entity Editor Traits

The editor needs per-entity-type behavior (ghost rendering, placement Y-offset) but entities live in `shared/` and must not know about the editor. `entities.def` generates a closed `entity_type` enum and plain structs; `entity_editor_traits.cpp` holds the editor-specific knowledge as one exhaustive switch over that enum per function, in the same style as `get_box_volume`, `create_map_entity`, `compute_entity_bounds`, etc. elsewhere in the codebase.

This used to be a per-type template specialization pattern (`Entity_Editor_Traits<EntityClass>`, four specializations required per entity). It was retired because the generated enum already made `-Wswitch` the exhaustiveness check — the template's linker-error-on-missing-specialization was solving a problem the switch solved for free, at the cost of ceremony (32 mandatory specializations for 8 types, most of them `return false;`) and hidden duplication (several types drew the exact same shape in both their ghost and in-editor specializations, just at a different position, because the mechanism gave no way to share that).

### How to add editor behavior for a new entity

Add the entity to `entities.def` as usual (see `entity_def.md`). Then in `entity_editor_traits.cpp`, `-Wswitch` will flag every switch (`get_placement_half_extents`, `draw_entity_ghost`, `draw_entity_in_editor`, `dispatch_selection_wireframe`) that needs a new case. Add one:

- **`get_placement_half_extents`**: the entity's half-size, used for the fallback wire box. Point entities (e.g. Particle_Emitter) return `{0,0,0}`. Entities with a `Box_Volume` component don't need a case here at all — that's handled generically before the switch.
- **`get_placement_origin_height`**: how far above the clicked surface the entity's **origin** goes, so it sits on that surface instead of clipping through it. Defaults to `get_placement_half_extents(e).y` — correct for the usual centered origin — but is **0** for the player-shaped types, whose origin is at the FEET (`player_constants.hpp`). Get this wrong and the entity is stored at a position the runtime reads differently than the editor drew it; that is exactly how spawns ended up 36 units in the air. If a new entity's origin is not at the center of its shape, it needs a case here.
- **`draw_entity_ghost`** / **`draw_entity_in_editor`**: return `true` if you drew something (most shapes are shared between the two, via a small static `draw_x_shape` helper — see `draw_player_spawn_shape` for the pattern), `false` to fall through to the default path (render component mesh wireframe, then wire box).
- **`dispatch_selection_wireframe`**: return `true` for a shape-specific selection outline, `false` to fall back to the AABB bounds wireframe.

### Runtime dispatch

Since placement works with `shared_ptr<Entity>` at runtime, `entity_editor_traits.cpp` exposes:

```cpp
linalg::vec3 get_placement_half_extents(const entities::Entity *e);
float        get_placement_origin_height(const entities::Entity *e);
bool         draw_entity_ghost(const entities::Entity *e, ...);
linalg::vec3 compute_placement_origin(const entities::Entity *e, const linalg::vec3 &ghost_position);
```

Each is a plain switch on `e->type` (no RTTI, no `dynamic_cast`). The placement tool just calls these and never needs to know about specific entity types.

## Box-Volume Dispatch (geometry layer)

The editor has two parallel dispatch mechanisms — pick the right one when adding an entity:

| Dispatch                              | Used for                                                | Mechanism                                                                       |
|-----------------------------------------|-----------------------------------------------------------|---------------------------------------------------------------------------------|
| `entity_editor_traits.cpp`'s switches | **Behavior-layer** facts (ghost icon, custom wireframe tint, placement Y-offset for entities without a box) | One exhaustive switch over `entity_type` per function                          |
| `get_box_volume()`                    | **Geometry-layer** facts (sculpting, picking, gizmo reshape, default placement extents)                     | Component-table lookup; one-line entry on box-owning entity                    |

Sculpting, the selection-tool gizmo-mode decision (Unified-reshape vs Translate-only), the gizmo's start-interaction / reshape-drag paths, the placement-tool default-extents seeding, and the fast-path in `get_placement_half_extents` all dispatch through `entities::get_box_volume(entity)` (returning `Box_Volume*`, generated from `entities.def`). Adding a new box-volume entity (clip-brush, hurt-volume, ...) requires zero edits in the editor — sculpting and picking come for free.

What stays per-type in `entity_editor_traits.cpp`: **behavior-specific drawing only.** Trigger volumes get a red selection wireframe, Lights get a cross gizmo. Those are type-level visual decisions, not geometry — they don't migrate to the component lookup.

The wedge dispatch sites in the gizmo and BVH go through `shared::entity_as<Wedge_Entity>` (closed-enum tag compare, no RTTI walk). Wedges will eventually grow their own `wedge_volume_t` + virtual following the same pattern as box-volume entities, at which point those `entity_as` calls collapse into the virtual too. See [src/shared/entities/README.md](../../shared/entities/README.md#box-volume-component) for the entity-side story.

## Trigger Actions

Trigger volumes (`Trigger_Volume_Entity`) are AABB zones that fire a named action when a player overlaps them. The named action is looked up at runtime from a server-side registry in `src/server/trigger_action_registry.hpp`. There is intentionally no scripting language — actions are plain C++ functions that self-register at static-init time, the same pattern used by `cvar::Console_Command`.

### How to add a new action

1. Add one line to the `TRIGGER_ACTION_LIST` X-macro in `src/shared/trigger_action_list.hpp`. The macro is a names-only list — no function pointers, no behaviors — so both client and server can read it without dragging server-only types into shared code.
2. In `src/server/trigger_actions.cpp`, write a function with the signature `void action_<name>(server::server_context_t&, Trigger_Volume_Entity&, Player_Entity&)`.
3. The X-macro at the bottom of that file already expands into self-registering statics — just adding the line in step 1 plus the function definition is enough, no manual `Trigger_Action_Registration` line.

### Why the server context is the first argument

Most non-trivial actions need world access — `warp_to_spawn` has to look up a `Player_Spawn_Entity`, `kill` has to route through the central `inflict_damage` helper to fire `PLAYER_DIED` and schedule a respawn. `server::server_context_t` bundles the game session, physics state, and event queue together, so any action that needs any of those gets them through one parameter. Lives in `src/server/` because all of these resources are server-only by design.

### Why the names list is shared but the dispatch is server-side

The editor inspector (which runs in the client DLL) needs to render a dropdown of valid action names. The server is the one that actually owns the function pointers. Splitting "names" from "behaviors" into two files lets the editor read the X-macro at compile time without needing to know what server-side machinery any given action calls into. The server's static-init registrations validate at startup that every name in the macro maps to a real function — a missing or misnamed entry log_errors instead of failing silently.

### Why string names, not integer IDs

If actions were keyed by integer (their order in some enum), prepending a new action would silently rebind every existing trigger in every saved `.map` file — `kill` (ID 0) would suddenly mean whatever you put at the top. Strings are stable: the name *is* the identity. Cost is ~12 bytes of `pascal_string` per trigger, which is negligible, and it makes the `.map` text format readable (`"action_name" "print_message"` versus `"action" "3"`).

### Parameter slots

Each action reads from a small fixed set of typed slots on the trigger entity:

- `param_target_name: pascal_string` — for actions targeting another named entity.
- `param_string: pascal_string` — generic string payload (message text, classname, etc.).
- `param_float: float32` — generic numeric payload (health amount, delay, etc.).

Three slots is a pragmatic compromise. The two canonical alternatives are:

- **Per-action parameter schema** (Unity UnityEvent, Unreal Blueprint): each action gets its own struct of typed args, serialized per instance. Requires discriminated-union schema support that this codebase doesn't have. Building it is a real undertaking and would dwarf the trigger feature itself.
- **Single stringly-typed parameter** (Source Engine I/O): one string field that each action parses. Maximum flexibility, minimum editor help.

Fixed typed slots split the difference: each slot gets a proper inspector widget, the schema system is unchanged, actions that don't use a slot just ignore it. Revisit if a future action genuinely needs a richer per-instance config.

### Fire modes

`fire_mode` is a `pascal_string` storing either `"on_enter"` or `"every_tick"`:

- **`on_enter`** fires once on the rising overlap edge — used for actions like `print_message` or `spawn_entity` that would otherwise spam at 60 Hz while the player stands inside.
- **`every_tick`** fires whenever the overlap is active. Used for idempotent actions like `kill` (firing every tick is harmless once health is 0) or continuous effects.

The choice is per-trigger, not per-action, so a designer reading the inspector sees exactly what will happen. Implementation cost is a `std::set<pair<trigger_id, player_id>>` on `server_context_t` that tracks last-tick overlaps so the server can detect the rising edge.

`fire_mode` is a `pascal_string` (not `int32`) on purpose: it reuses the same `Field_Prop::string_choices_provider` mechanism as `action_name`, which means the inspector renders a dropdown the same way for both fields and there is no special-case code path. The original plan stored `fire_mode` as `int32` with a hardcoded combo case in the inspector; we collapsed it to a string field because adding a one-off special-case for two values is worse than reusing the general mechanism we already had to build for `action_name`.

### Why `string_choices_provider` is a callback, not a fixed list

The provider is `std::function<std::vector<std::string>()>`, evaluated at inspector render time — not at schema registration time. This matters because static-init order between translation units is undefined: actions registered in `trigger_actions.cpp` may register *after* `Trigger_Volume_Entity::register_schema()` runs in another TU. A fixed-list provider captured at registration would be empty in that case. Calling the provider at render time guarantees correct results regardless of init order.

### Spatial query strategy: linear scan now, sensors later

Trigger overlap is evaluated as an O(triggers × players) linear scan in `server_impl.cpp`'s `Tick()`. The canonical alternative is **physics sensors** (Jolt: `BodyCreationSettings::mIsSensor`; Unity: `Collider.isTrigger`; Unreal: overlap-only collision; Source: `SOLID_TRIGGER`) — the broadphase prunes pair tests and "is X inside Y?" reuses the same overlap pipeline as everything else.

We deliberately kept the linear scan for three reasons:

1. With ~10 triggers and ~10 players the inner loop is 100 AABB tests per tick — below the noise floor.
2. Jolt integration is in flight (recent commits). Wiring sensor bodies through `physics_state_t` would conflate two unfinished threads and make the trigger work depend on physics work that may shift.
3. Linear is independent of which physics system wins. The code keeps working until we replace it deliberately.

**Migrate to Jolt sensor bodies** the first time *any* of these is true:

- Trigger count exceeds ~50 per map.
- Per-tick trigger evaluation shows up in a profile.
- Jolt sensor bodies are first-class for at least one other entity type (so the wiring is already paid for).
