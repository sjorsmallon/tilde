# Entity Definition System — Final Design

> **This file is a DESIGN document, not a plan. Disregard every ordering,
> sequencing or "do this first" claim in it — including the "Order of
> operations" section at the bottom, which is now a stale subset.**
> `todo.md` is the single plan of record: it carries the full phase order
> (P0–P8) with the undo/redo and string-invariant work this file never
> accounted for. Read this file for *what the system is and why*; read
> `todo.md` for *what to do next*. If the two disagree about order, `todo.md`
> wins — and the drift is a bug in this file, so fix it here rather than
> re-planning in place.

Settled design for replacing the macro-based schema/entity system with a
generator. Outcome of the design discussions on 2026-07-26.

## Core principle

**Every fact about an entity is stated in exactly one place (the DSL), and
everything else is derived.** The generator is not valuable because it saves
typing — it's valuable because it converts architectural decisions from
permanent to revisable. Inline → handle? Generator change. memcmp → dirty
bits? Generator change. Kill protobuf? Generator change.

Optimize the two metrics that actually matter for a solo project:
- lines touched to add entity #10
- places to look when something breaks

"Set up for success" ≠ build the general thing now. It means: build the
specific thing with clean seams so the general thing can slot in where the
future is known to knock.

Second razor: **the generator may derive, but never invent invisibly.** Tags
derive from declared names, collections derive from declared flags/components,
the base prefix comes from a `base` block declared in the .def file itself —
everything traceable to a visible line; nothing hardcoded in the tool.

## Scope: three regimes, one language

The DSL covers exactly one thing: **replicated gameplay records (entities and
their components).** The full division of the world:

| Regime | What | Constraints | Owned by |
|---|---|---|---|
| **Records** | entities + components | blittable, fixed-size, declared in DSL | generator |
| **Geometry** | brushes, displacements, static world | plain C++ value types, std::vector fine | map module (handwritten) |
| **Assets** | meshes, sprites — shared authored data | files on disk | asset system + generated manifest |

Plus **sidecars**: derived runtime state (GPU handles, physics bodies) owned
by the systems that need it, keyed by uid/handle — never inside records.

Each regime pays only its own constraints. The schema system's restrictions
(blittability, fixed size, memcmp-diffing) exist *for networking*; geometry
is never networked, so it does not live in the DSL (see the Geometry section
for the full rationale — this was relitigated and settled).

## What this replaces (and why)

The current macro system has structural debts, not just aesthetic ones:

- **Fields declared twice**: `SCHEMA_FIELD` in the header + `REGISTER_SCHEMA_FIELD`
  in the .cpp, plus `DECLARE_SCHEMA`, `SCHEMA_NAME_FOR_TYPE`, an X-macro line,
  and an `entity_list.hpp` include per entity.
- **Static-init registration fragility**: anonymous-namespace init TUs get
  linker-dropped from the static lib (bitten twice: CVar singleton, schema
  registries). Generated tables are compile-time data → this bug class becomes
  structurally extinct, not worked around.
- **`offsetof` on non-standard-layout types**: UB, pragma-suppressed in
  schema.hpp. Dies when entities become plain structs.
- **Flat (offset, size, memcpy) field model**: can only describe fields as
  blobs; can't express nesting or per-field code.
- **Unenforced flags**: only `Editable` was ever checked anywhere
  (entity_inspector.cpp). `get_all_properties` saved everything; the wire path
  serialized everything. `Networked`/`Saveable` were decorative — hence the
  inconsistent annotations across current entities.

## The DSL

Plain text `.def` files, checked into git (diffable, mergeable, greppable).
**Not** an interactive tool (opaque state), **not** annotated-C++ parsing
(UHT-style effort trap). A dumb line-based parser, a few hundred lines, written
in C++, built as a host tool before the libs.

Jai/Odin-flavored syntax: `Name :: kind { ... }` declarations, `name: type =
default` fields, trailing `@` annotations, `.EnumValue` literals,
newline-terminated, `//` comments. Kinds: `base`, `component`, `entity`,
`enum` (+ flagset constants). Structs are **pure data — no methods**.

```
// The one base. Prefixed onto every entity. No inheritance syntax exists —
// deeper hierarchies are inexpressible by construction (variants are enum
// fields; sharing is components).
Entity :: base {
    entity_id:   u32   @Networked
    position:    v3    @Networked @Editable
    orientation: v3    @Networked @Editable
}

Full_Serializable :: [@Networked, @Editable, @Saveable]

Light_Type :: enum { Point, Spot, Directional }

// components: reusable field groups; no tag, no factory entry
Box_Volume :: component {
    half_extents: v3 = {16, 16, 16}   @Full_Serializable
}

Render_Model :: component {
    mesh: mesh_asset                  @Full_Serializable   // manifest type, see Assets
    tint: v3 = {1, 1, 1}              @Networked @Editable
}

// entities: tag/classname/factory derived from the name; implicit base prefix
Light_Entity :: entity {
    direction: v3                     @Networked @Editable
    color:     v3 = {1, 1, 1}         @Networked @Editable
    intensity: f32 = 1.0              @Networked @Editable
    kind:      Light_Type = .Spot     @Networked @Editable
}

Trigger_Volume_Entity :: entity {
    volume:       Box_Volume
    action:       Trigger_Action = .Kill    @Full_Serializable
    fire_mode:    Fire_Mode = .On_Enter     @Full_Serializable
    target_name:  string<32>                @Full_Serializable
    param_string: string<64>                @Full_Serializable
}
```

- No `type LIGHT` declaration: the enum constant is the struct name
  (`entity_type::Light_Entity`), values auto-numbered in declaration order
  (values need no stability — network is hash-checked, disk is name-based).
- No collection declarations: every component and every flag automatically
  defines a collection; the generator emits the tables and `for_each_with`.
- Display names are **derived** (strip `_Entity`, underscores → spaces); no
  `@display_name`. No `@category` — the placement menu is a flat list of a
  handful of types.
- **Flag composition rule**: a component's own field flags are the truth; no
  per-use masking on component fields (v1).
- Open (decide at grammar freeze): struct-level `@default_flags(...)` vs
  explicit per-field flags (lean: explicit).

## Annotation reference (the complete closed list)

- **Field flags (3)**: `@Networked` (replicated), `@Editable` (inspector
  widget), `@Saveable` (written to map file).
- **Class marker (1)**: `@runtime_only` — "no map file may contain this
  type." This is a **persistence-domain fact**, the type-level analog of
  `@Saveable`: the map loader enforces it (rejects the classname loudly); the
  placement menu merely derives from it. Litmus test for annotation
  legitimacy: if the editor were deleted, would it still mean something?
  `@display_name`: no → died. `@runtime_only`: yes → stays.
  (Player/Rocket/Weapon are `@runtime_only`; Player_Spawn exists because
  Player is.)
- **Flagsets**: user-defined `Full_Serializable :: [@Networked, @Editable,
  @Saveable]` — same `@`-atoms at definition and use site; pure parse-time
  aliases, no semantics of their own.
- **That is the entire surface. There are zero field options.** `@choices`
  died with the asset manifest (see Assets). Documented-future, not
  implemented: `@range`, `@bits`/quantization, `@interpolate`, `@was`.
- Grammar: `name: type = default  @Flag` — value-space (`=`) strictly
  separated from meta-space (`@`).
- **Closed and validated**: unknown annotation = hard generation error with
  file/line. Never silently dropped.
- **Enforcement history (why validation matters)**: in the macro system only
  Editable was ever enforced; Networked/Saveable were decorative. The
  generator makes flags real, which CHANGES behavior (e.g. Light fields lack
  Saveable today and would silently stop saving) — so the .def transcription
  is a **mandatory conscious re-audit of every field's flags**, not a copy.

## Type system

- Primitives: `f32 f64`, `u8 u16 u32 u64`, `i8 i16 i32 i64`, `bool`,
  `string<N>`. No pointers, period.
- **Vectors are primitives** (`v3 v4 v4i`), not nested schemas. Rule: a type
  is a primitive iff it's atomic in every consumer — one editor widget, one
  wire encoding, one diff entry. Same rule admits quaternions/colors later.
- **Enums declared in the DSL** → generated C++ enum class,
  `to_string`/`from_string`, editor combo, minimal wire bits, name-based disk
  I/O. Replaces the string-typed enums-in-disguise (shader_type, shape_type,
  action_name, fire_mode).
- **Asset types from the manifest** (see Assets): `mesh_asset`, `sprite_asset`
  — closed sets, enum-like treatment.
- **Fixed-capacity inline arrays** `[N]T` (static capacity, runtime count,
  count-prefixed on wire, trivially copyable): kept in the grammar but
  demoted — its only large user (displacement heights) left with geometry.
  If no entity needs it, drop from v1.

### Strings

- **Strings are the fallback type, not the default.** Audit of current usage:
  of 8 pascal_string fields, 4 are enums in disguise, 2 are asset references
  (→ manifest types), 1 is a by-name entity reference (target_name — keep
  name-based, Source targetname model), 1 is a genuine string (param_string).
- **Per-field capacity**: `string<32>` → `pascal_string_t<32>` (template
  already exists; only the schema layer hardcodes <250>). Small default
  capacity; larger is explicit. Wire count bits sized from capacity.
- **Canonical zero-padding invariant**: `set()` zeroes the tail; memcmp is
  exact string equality. (Live bug in current set() — no tail zeroing, c_str()
  can return merged garbage after shrink, phantom deltas. In todo.md; fix
  independent of the generator.)
- **Truncation is an error**: editor validates capacity at input; over-length
  map value = loud error; set() asserts/returns bool. No silent truncation.
- **No interning / string tables** — chat/UI text goes over messages, not
  entity state.

## Assets: the generated manifest

Assets look like an open set but aren't — **they're files in the repo,
knowable at build time**, same as everything the generator sees. A build step
scans the asset directories and generates a manifest: closed id↔path/name
tables per asset class (`mesh_asset`, `sprite_asset`).

- DSL fields use the manifest type: `mesh: mesh_asset`. Closed set → the
  inspector combo works by the same mechanism as any enum. No annotation.
- **Name on disk, id on wire** — stable across renumbering, compact on the
  network; the generic enum treatment, not special magic.
- Kills the hand-maintained `assets::get_mesh_path(asset_id)` mapping.
- This is what replaced `@choices` / `string_choices_provider`: after enum
  conversion, its only remaining job was asset picking, and a closed
  generated set does that with zero DSL surface.

## Generated artifacts (the output contract)

Generated code must be *readable* C++ you can step through in a debugger.
**Zero static initializers** — everything is constexpr tables and plain
functions. Prefer emitting code (switches, functions) over data (tables)
wherever the code variant unlocks capability.

1. **Schema tables (reflection data).** Per type: a constexpr array of field
   records `{name, field_type, offset, size, flags}`. Offsets are real
   offsetof — legal on the standard-layout structs. Component-typed fields
   point at the component's own table (the recursive tree). Consumers: editor
   inspector, map text I/O.
2. **Serialization: generic entry points, generated bodies.** Per-type
   straight-line functions (`serialize_light_entity(writer, entity,
   baseline)`, `deserialize_...`), composing recursively into component
   serializers. Public API is a free function: `serialize_entity(writer,
   entity, baseline)` — reads the tag off the entity itself (never passed
   separately), asserts validity, dispatches through the table.
3. **Collections: generated tables, handwritten helpers.** Generated:
   `component_type` enum, `component_mask` per entity type,
   `component_offset[tag][id]` (byte offset or -1),
   `types_with_component[id]`, `component_id_of<T>`. Handwritten once in a
   small runtime header (part of the output contract): `has_component`,
   `get_component<T>`, `for_each_with<T>` — thin loops over the tables.
   **API principle: tables are implementation detail** — internal linkage in
   the generated TU; the public surface is free functions that hide every
   lookup and assert on invalid tags. Callers never index `entity_type_info`.
4. **Construct / copy / clone.** DSL defaults become default member
   initializers → construction is `T entity{};`; copying is `=`/memcpy
   (blittable). For type-erased callers (undo, baselines, factory):
   `entity_type_info[tag].size_in_bytes` + alignment, and
   `entity_type_info[tag].construct_at(void *memory)`. Heap factories
   (`create_entity(tag)`, `create_entity_by_classname(name)`) are thin
   wrappers.
5. **Enum support**: generated enum class + `to_string`/`from_string` + value
   count. Asset manifest tables (id ↔ name ↔ path).
6. **Identity & metadata tables**: `entity_type` enum (replaces the X-macro),
   classname strings, derived display names, `@runtime_only` bits, per-flag
   field subsets.
7. **Schema hash**: canonical digest of all declarations, baked in as a
   constant for the connect handshake.

### Behavior: free functions — no handwritten class half

Structs are pure data (the Jai/Odin stance). The generator emits the complete
struct; behavior lives in free functions in client/server code
(`fire_trigger(session, trigger)`, not `trigger.fire()`). No generated-base /
handwritten-derived split exists.

## Entity model: plain non-virtual structs

Every current virtual is a per-type constant in disguise:

| Virtual | Becomes |
|---|---|
| `get_type()` | stored tag field (first member, set by factory) |
| `get_schema()` | `entity_type_info[tag].schema` |
| `is_collision_geometry()` | obsolete — static geometry left the entity system entirely |
| `get_box_volume()` | `get_component<Box_Volume>()` via the component tables |
| `init_from_map()` / `get_all_properties()` | schema-driven free functions; compat shims die via one-time map conversion |
| `~Entity()` | not needed (shared_ptr type-erases the deleter; fields trivially copyable) |

Result: genuinely trivially copyable entities; memcpy snapshots become
blessed. `entity_as<T>` unchanged (already a tag compare + static_cast).
`Entity_Of` (a tag-injection mixin, not real CRTP) shrinks to nothing.

### Amended 2026-07-26: entities derive from a generated `entity_base_t`

Originally this section assumed the base fields would be spliced flat into
every entity struct, making them standard-layout and `offsetof` legal. That
was implemented, then reversed on review, because it left **no legal way to
spell "pointer to any entity"** — with a flat prefix, `(entity_base_t*)&light`
is a strict-aliasing violation, blessed by the standard only for structs
inside a union ([class.mem]/23).

The choice is which of two formally-UB-but-universally-working things to lean
on, not whether to have one:

| | flat prefix | inheritance (chosen) |
|---|---|---|
| `light.position` | works | works |
| base pointer | strict-aliasing UB | **legal** (derived-to-base) |
| `offsetof` in field tables | legal | conditionally-supported UB |
| trivially copyable | yes | **yes** — unaffected |

Inheritance wins because its UB is *the one already shipped today* (schema.hpp
pragma-suppresses exactly this), and trivial copyability — the property that
actually protects memcpy snapshots and memcmp diffing — survives either way.
A nested `base` member would make both legal but forces `light.base.position`
at every call site; rejected on ergonomics.

Crucially, **flat prefix and inheritance produce identical call sites**, so
switching between them later is a change to one emit function with zero churn
in consuming code. Only the nested-member variant would have migration cost.
`entity_layout_test` static_asserts trivial copyability and the base
conversion so a future emit change can't silently lose either.

## Components: inline, no pointers

- Components are **value members** (inline). Whole-entity memcpy/memcmp stays
  valid — pointers would break it (copy aliases, compare diffs the pointer
  value, stored pointers are meaningless in snapshots).
- The **schema model is recursive** (a schema field references another
  schema); the memory is flat. `[3,2]`-style addressing falls out of the tree.
- Storage class is a DSL attribute: `inline` now, `handle` reserved. If
  polymorphic components ever arrive (point vs skeletal body): **handles into
  per-type pools + component-type tag**, never raw pointers.

### Why not Valve's pointer route

Source 2 uses pointers because: (1) their components are runtime-polymorphic
(inline storage impossible), (2) their change detection is push-based
(NetworkStateChanged dirty ticks — they never relied on blittability),
(3) hundreds of entity classes with sparse component sets, (4) their
serializer had to follow an existing giant C++ codebase. **Zero of these
apply here.** We design both sides of the contract — spend that luxury on
representations that make serialization easy.

## Filtering entities by component

Key asymmetry vs real ECS: component ownership is per-**type**, not
per-**instance** — a compile-time fact. Filtering is a codegen problem, not a
data-structure problem: **the DSL is the archetype table.**

- `has_component` = one mask bit test; `get_component<T>` = one table lookup
  + add (replaces the current schema-walk, which also can't distinguish two
  same-typed fields).
- Tier 1 (now): linear scan + mask test — nanoseconds at this entity count.
- Tier 2 (free upgrade if storage becomes per-type pools): iterate only the
  buckets in `types_with_component[X]`.
- `for_each_with<T>(session, fn)` — call sites identical across tiers.
- **Optional components (presence bit) are a smell — keep them rare.** Often
  "AABB with physics" is honestly a second entity type in the DSL.

Recurring pattern: before answering any query with runtime machinery, ask
whether the DSL already knows the answer statically.

## Geometry: outside the DSL entirely

**History**: brushes became entities to get one editor object model. That
bundled two decisions — (A) geometry as uniform editable objects: correct,
kept; (B) geometry as runtime entities: a mistake, whose scar tissue was
`is_collision_geometry()` routing entities *out* of the entity system at
session init. A first fix attempt (a `geometry` DSL kind + opaque `payload`
slots for bulk data) was rejected as contortion: type-erasing height arrays
into blobs just because the record model can't hold variable-size data.

**Diagnosis**: the schema system's constraints (blittable, fixed-size,
memcmp-diffable) exist for networking. Geometry is never networked. It was
paying constraints it got nothing from — and the `[64]f32` heights cap was
the format's limitation leaking into what the game can express.

**Resolution**: geometry is **plain C++ value types owned by the map
module**. Real types, no caps, no erasure:

```cpp
struct displacement_t
{
  transform_t transform;
  material_id material;
  u32 resolution;
  std::vector<f32> heights;   // assert(heights.size() == resolution * resolution)
};
```

- Map save/load: handwritten per geometry type (four small functions —
  box/wedge/static-mesh-ref/displacement, a set stable for decades in
  Source-likes). Session init reads geometry straight into the static BVH and
  render batches. Never replicated; distributed via map streaming only.
- Inspector: four small handwritten panels (3–6 properties each) — arguably
  better than generated (custom widgets).
- Undo: whole-value snapshots (structs copy; sculpting already does
  tool-owned start-state capture). The transaction system carries two entry
  flavors: entity field-diff and geometry value-swap.
- **The editor seam that preserves uniformity**: selection, gizmo, picking
  BVH, and transactions need only *transform + bounds + hit-test +
  snapshot/restore* — an interface both regimes implement. `map_t` holds both
  lists; editor tools iterate "editable map objects" and don't care which
  regime backs them. Uniform editing never actually required schemas.
- `Box_Volume` the *component* stays — for entities that need a box at
  runtime (Trigger_Volume, Physics_Body). Brushes just have box members.
- Fallback if geometry types ever multiply (they won't soon): a relaxed
  non-blittable DSL kind with real `[]T` dynamic arrays and no wire
  serializers. Decision reverses cheaply.

**Rendering data splits by the same razor** — *edited per-field → schema
(Render_Model: model choice, tint); bulk authored → asset files (mesh data,
via manifest); derived at runtime → sidecar (GPU handles, uploaded buffers).*
The original sin was one system playing all three roles.

## Serializer architecture

One narrow seam: **"give me the changed fields."** Change *detection* is a
separate module from wire *encoding*:

- Detection today: memcmp against baseline (simplest correct thing; protected
  by blittability). Later: dirty-bit / change-tick tracking swaps in here
  without touching encoding.
- Encoding today: bitstream write of field indices + values. Later: snapshot
  delta compression / field-path encoding swaps in here without touching
  detection.

Serializer v2 (when snapshot delta compression starts): recursive generated
visitors over the schema tree replace the flat `Field_Prop` walk.

Two seams shaped now, built later:
- **Change notification**: the generated deserializer already knows which
  fields it wrote — its signature must be able to grow an optional
  changed-field bitmask out-param (client reactions: "mesh id changed →
  reload asset") without restructuring.
- **`@interpolate`**: client-side snapshot lerping as a future field
  annotation; nothing to reserve except knowing it lands there.

## Versioning

- **No version numbers, no migration chains.** Two mechanisms:
  - **Network**: the generated schema hash, exchanged at connect; mismatch →
    refuse loudly with both hashes.
  - **Disk**: name-based self-describing text. Additive changes free (missing
    key → DSL default); removals ignored with a logged warning; renames via
    **one-time map file conversion** (all maps are owned, text, name-keyed).
    The "center"/"half_extents" compat shims die the same way. `@was` alias
    deliberately NOT built — revisit only when maps exist we don't own.
  - **Deterministic save order**: declaration order — deliberately
    git-diffable (today's std::map alphabetical order is only accidentally
    deterministic).
- Corollary: field indices are purely internal — regenerated freely every
  run. No protobuf-style reserved-tag bookkeeping. Keep it that way.

## Explicitly not building (with the seam where each would land)

| Don't build | If needed later, lands at |
|---|---|
| Polymorphic components | DSL storage class `handle` + per-type pools |
| Field-path wire encoding | encoding side of the serializer seam |
| Pointer chasing in serializer | never — handles beat pointers in this model |
| Shared components between entities | probably never — no clean identity on wire/undo |
| Dirty-bit change tracking | detection side of the serializer seam |
| Deeper inheritance | DSL grammar (likely never wanted) |
| Archetype/sparse-set machinery | tier-2 bucketed storage (likely never needed) |
| String interning / string tables | never for entity state — chat/UI goes over messages |
| Payload/blob slots in the DSL | never — geometry left the schema system instead |
| Editor-hint annotations (@display_name, @category, @choices, @range) | derivation, the kind split, the asset manifest; @range as future option |
| `@was` disk aliases | one-time map conversion until user-made maps exist |

## Tie-in to the entity system

**Principle: the generator describes types; the entity system stores them.**
The generator emits no storage policy — no pools, no allocation, no lifetime.
It emits per-type facts (size, alignment, construct_at, tables); the entity
system is handwritten generic code over those facts.

**The unlock: value semantics.** shared_ptr<Entity> exists because
polymorphic types force reference semantics. Blittable structs + a size table
dissolve that: editor placements can hold entities by value; undo snapshots
become inline byte copies; and the natural storage end-state is **per-type
pools** (one vector<T> per entity type) — world snapshot = memcpy per pool,
tier-2 filtering free, cache-coherent iteration. Identity: `entity_id` uid
(already networked) → `{tag, pool index}` handle map owned by the session.
Deferred decision, made when pools land: generation counters on slots — start
without; destroyed-slot access asserts either way.

**Sidecar rule: entities hold only the replicated/saved truth.** Derived
runtime state (loaded-asset pointers, physics handles, render caches) lives in
system-owned sidecar structures keyed by uid/handle — the pattern
physics_body_entity.hpp already documents ("the owning physics_state_t holds
the actual Jolt body"). **No `@transient` escape hatch in the DSL, ever** — it
would reintroduce arbitrary C++ types into generated structs and erode
blittability one convenience at a time.

**Lifecycle hooks are handwritten exhaustive switches.** Per-type logic at
load/spawn/destroy (e.g. server consumes Player_Spawn at load and removes it)
lives in free functions like `on_entity_spawned(session, entity)` switching on
the closed enum — the compiler warns on unhandled cases, which a forgotten
virtual override never did.

**Phasing rule: generator v1 must not require the storage refactor.** v1
generated entities work inside today's shared_ptr<Entity> session storage via
the heap factory wrapper. Pools are a separate later step, after
de-virtualization completes. Never do the storage rewrite and the reflection
rewrite in the same breath.

## Open questions (raised 2026-07-26, deliberately not decided yet)

### 1. Sentinel values in generated enums — `Invalid = 0` and `Count`

The generator currently emits three different conventions by accident, not by
rule: `entity_type` has both `Invalid = 0` and `Count`; `component_type` has
`Count` but no `Invalid`; DSL-declared enums (`Light_Type`, `Fire_Mode`, …)
have neither. Pick a rule and apply it uniformly.

The two are **different mechanisms and should be judged separately** — treating
them as one "sentinel" habit is the thing to avoid:

- **`Count` is not a domain value**, it's metadata about the enum, and putting
  it inside actively defeats the exhaustive-switch warning this design relies
  on for lifecycle hooks. A switch covering every real value still warns about
  unhandled `Count`; silencing it with `default:` permanently kills the warning
  that a *newly added* value went unhandled. Hand-written enums embed `Count`
  because there's nowhere else to put it — generated ones can emit a separate
  `ENTITY_TYPE_COUNT` constant that sizes arrays identically at no cost.
- **`Invalid = 0` is honest only where "none/unknown" is a real state.** It
  earns its place in `entity_type` twice (classname lookup must return
  something on failure; zeroed memory must not read as valid). It does *not*
  belong on `Light_Type` — a light always has a type, so `Invalid` would
  manufacture a value that is never legitimate and force every switch to
  handle a case that cannot happen.

Leaning: `Count` never inside the enum; `Invalid` only where the domain truly
has one. Caveat that reverses it: if switches end up using `default:` anyway,
`Count` costs nothing and the familiar `i < (int)entity_type::Count` idiom is
arguably nicer. Decide whether exhaustive switching actually holds in practice.

### 2. Missing construction and factory helpers

The generator emits reflection and identity but **nothing that creates an
entity**. Artifact #4 in the output contract above specifies these; none exist
yet:

- `entity_base_t* entity_from_classname(const char* name)` — the editor and map
  loader both want an instance, not a tag. `entity_type_from_classname()`
  currently returns only the `entity_type` tag.
- `create_entity(entity_type)` — heap factory over the same switch.
- `entity_type_info_t::construct_at(void* memory)` — the type-erased hook that
  undo, baselines and pooled storage need. `entity_type_info_t` already carries
  `size_in_bytes` and `alignment`, so this is the missing third piece.

Open sub-question: with entities now deriving from the base struct, the factory
can legitimately return a base pointer — this is exactly the case the
inheritance decision (see the 2026-07-26 amendment above) was made to serve.
Worth confirming the return type is a pointer and not a handle before pooled
storage lands, since that choice is harder to reverse once callers exist.

**Answered 2026-07-30 (`entity_storage_def.md` §2, §4): both, split by path.**
There is no single factory return type because there were never one caller's
worth of callers. The *runtime* path hands out a handle —
`Entity_System::spawn<T>()` returns an `entity_uid_t`, resolved at point of use
by `get<T>(uid)`. The *map* path keeps returning a pointer:
`create_map_entity(classname)` yields a `shared_ptr<entities::Entity>` because a
map entity is editor-owned storage that has no session identity to hand a handle
against. The two converge at `Entity_System::add_entity`, which is where the
copy into the pool happens and where the uid is stamped.

And the handle is the **bare `entity_uid_t`**, not a generational pair — the uid
is already the identity on the wire, in map files and in Jolt's body map, so a
generational handle would have been a second identity for one thing. Full
reasoning in `entity_storage_def.md` §2.

### 4. ~~Base struct named `entity_base_t`~~ — FIXED 2026-07-26

The emitter hardcoded the name `entity_base_t` while the .def declared
`Entity :: base { … }` — a straight violation of the second razor ("the
generator may derive, but never invent invisibly… nothing hardcoded in the
tool"). Not a naming preference: a bug.

Fixed by emitting the `base` declaration's own name. The API now reads
`Entity* entity = entity_from_classname("light_entity")`, and renaming the base
is now an edit to the .def like every other fact. Verified by renaming the base
to `Game_Object` in a scratch copy and confirming the base struct and all eight
derived structs followed, with no hardcoded name surviving anywhere.

No collision with today's `network::Entity`: generated code lives in
`namespace entities`, and `network::Entity` dies at the end of the
de-virtualization step regardless.

### 3. Enumerating placeable entity types for the editor

`entity_type_info_t` carries the `runtime_only` bit, but nothing enumerates the
types that *aren't* runtime-only, so the placement menu has no source of truth
to populate from. A caller can loop `1..ENTITY_TYPE_COUNT` and filter, but that
means every caller re-derives the same filter and indexes the table directly —
which contradicts the "tables are an implementation detail, the public surface
is free functions" principle in the output contract.

Wants a generated helper (shape TBD): either a `placeable_entity_types()` span
over a constexpr filtered array, or a `for_each_placeable_type(fn)` callback.
The array form is probably better — the editor wants to index it for a menu.

## Order of operations

**Revised 2026-07-26. There is no compatibility phase.** The original plan had
generator v1 emit the existing `Class_Schema` shape so macro and generated
entities could coexist against one registry, migrating one type at a time. That
is abandoned: this is a solo project, so the cost of a hard cutover is a day of
compile errors, and the cost of the compatibility layer was building an entire
throwaway emitter plus reasoning about two live reflection systems at once. The
generator emits the end state directly and always has.

Two orderings settled at the same time, both load-bearing:

- **Geometry leaves before the generator is wired in.** `Displacement_Entity`'s
  `schema_array_t<float32, 3267>` is the *only* array-typed field on any entity
  (verified by survey). Letting geometry exit first means the DSL never needs
  fixed-capacity arrays at all, instead of building `[N]T` and deleting it a
  step later. It also drops the generator's workload from 12 types to 8.
- **The storage rewrite stays separate — and this is not about compatibility.**
  A reflection change breaks *compilation*, so the compiler hands you every
  site to fix: bounded and verifiable. A storage change (shared_ptr → pools)
  mostly compiles fine and fails at *runtime* with dangling handles and stale
  indices. Bundled, a misbehaving game gives no signal about which half did it.

1. **Geometry exit.** AABB/Wedge/StaticMesh/Displacement become plain
   map-owned C++ value types (`std::vector` fine, never networked), with
   handwritten map I/O and four small inspector panels. Session init stops
   consulting `is_collision_geometry()` and loads geometry straight into the
   static BVH. One-time map conversion. The editor seam (transform + bounds +
   hit-test + snapshot/restore) is what keeps editing uniform across regimes.
2. **Finish the generator.** The open items above: factory / `construct_at` /
   `entity_from_classname`, placeable-type enumeration for the editor, the enum
   sentinel rule, and the asset manifest scanner (kills the `mesh_asset`
   placeholder typedef).
3. **The flag audit.** Non-negotiable before generated code drives anything —
   `@Networked`/`@Saveable` were decorative in the macro system and become real
   here. Two known-bad cases are already marked `FIXME(audit)` in entities.def.
4. **Hard cutover.** Delete the macro system and rewrite consumers in one pass:
   ~9 files on `Class_Schema`/`Field_Prop`, ~21 on the X-macro / factory, ~94
   dynamic-dispatch sites (`entity_as`, `get_schema`, `get_box_volume`,
   `is_collision_geometry`). `network::Entity` and its virtuals die here.
5. **Serializer v2** with the snapshot-delta-compression work: recursive
   visitors, detection/encoding seam, retire the flat field walk.
6. **Storage refactor** (per-type pools, value semantics, handle map) — its own
   pass, after the dust settles, for the reason above.
7. **Protobuf removal** when it annoys enough — by then the generator emits
   message serialization, so it's absorption, not a project.

Steps 1–3 ship independently and the game runs throughout. Step 4 is a hard
break by design: the tree does not build from the moment the macros are deleted
until the last consumer is converted. That is the point — the compiler is the
migration checklist.
