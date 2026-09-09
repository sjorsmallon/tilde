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

**The registry this section described is gone, twice over.** It was a
string-keyed, self-registering table (`trigger_action_registry.hpp`, the
`TRIGGER_ACTION_LIST` X-macro, a `string_choices_provider` callback, three typed
`param_*` slots and a `fire_mode` field on the trigger); that became a closed
`Trigger_Action` enum, and that is gone too. A trigger volume now says only
WHEN — it emits `Touched` on the rising overlap edge and `Left` on the falling
one — and what a touch DOES is a `connection` row in the map, aimed at any
entity rather than only at the toucher. See "Entity I/O" in CLAUDE.md and
`entity_io_def.md`.

Two of the old arguments are worth keeping, because entity I/O reached the same
answers by another route:

- **"Why string names, not integer IDs"** — prepending an action must not
  silently rebind every saved trigger. A connection names its signal and its
  action by NAME in the file, and `SCHEMA_HASH` covers the declarations, so a
  reorder is a refused handshake rather than a silent remap.
- **"Per-action parameter schema"**, listed as the ideal and dismissed as
  needing discriminated-union support the codebase did not have, is exactly what
  `action_data_t` and the per-verb payload structs are now. `def_gen` builds the
  union and the field tables, so each action carries its own typed parameters
  and the editor gets a proper widget per field — the three fixed `param_*`
  slots were the compromise that bought.

The `fire_mode` field went with them: `Touched` is the rising edge and `Left` the
falling one, which is what `on_enter` meant, and a per-tick effect is now a
`Touched` that enables plus a `Left` that disables rather than a re-fire at 60Hz.

### Spatial query strategy: linear scan now, sensors later

Trigger overlap is evaluated as an O(triggers × players) linear scan in `src/server/systems/trigger_system.cpp`, called once from `Tick()`. The canonical alternative is **physics sensors** (Jolt: `BodyCreationSettings::mIsSensor`; Unity: `Collider.isTrigger`; Unreal: overlap-only collision; Source: `SOLID_TRIGGER`) — the broadphase prunes pair tests and "is X inside Y?" reuses the same overlap pipeline as everything else.

We deliberately kept the linear scan for three reasons:

1. With ~10 triggers and ~10 players the inner loop is 100 AABB tests per tick — below the noise floor.
2. Jolt integration is in flight (recent commits). Wiring sensor bodies through `physics_state_t` would conflate two unfinished threads and make the trigger work depend on physics work that may shift.
3. Linear is independent of which physics system wins. The code keeps working until we replace it deliberately.

**Migrate to Jolt sensor bodies** the first time *any* of these is true:

- Trigger count exceeds ~50 per map.
- Per-tick trigger evaluation shows up in a profile.
- Jolt sensor bodies are first-class for at least one other entity type (so the wiring is already paid for).
