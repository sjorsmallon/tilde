# Asset Pipeline — Design

Sibling of `cvar_def.md` and `events_def.md`: the same generator discipline,
pointed at the resource tree. Outcome of the design discussion on 2026-08-21;
revised 2026-08-22 after auditing `asset.cpp` itself, which added inventory
items 7-12, the "Failure" section and step 0 — the original draft covered how
assets are *provisioned* and said nothing about how they are *loaded*.
Read `entity_def.md` first — single declaration point, derive-never-invent, no
registration as a runtime concept, closed sets, loud failures. All of it carries
over and is not re-argued here.

This replaces `src/shared/assets/assets.def` and the `assets` declaration kind
in `def_gen`. Both are deleted.

## Why

The asset family was the first `.def` written, and it is the one that does not
fit the family test CLAUDE.md states for itself:

> Every `.def` family exists because two parties must agree on a declaration
> (client/server, fire-site/handler, disk/code).

For the **manifest** there genuinely are two parties — the filesystem, and the
code that says `mesh_asset::Pyramid`. That agreement is real and worth
generating. For a **scan directive** there is no second party. It is build
configuration, and the evidence that it is build configuration is that it
already lives in the build:

```cmake
# CMakeLists.txt:190-194 -- a hand-maintained mirror of assets.def's scans
file(GLOB ASSET_GEN_SCANNED_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/obj/*.obj
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/models/*.mesh
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/sprites/*.png
)
```

Nothing checks that the two agree. Add a scan to the `.def` and forget CMake,
and dropping in a new file silently fails to regenerate the manifest.

The rest of the inventory:

1. **`procedural` is the `__primitive_` prefix reincarnated.** It deleted seven
   `strncmp` dispatch sites and bought six `strcmp` sites in
   `generate_mesh_for_key`. A mesh that is code is a function, not an asset.
2. **`source_kind` is provisioning leaking into identity.** The table needed a
   comment telling readers not to look at one of its columns, and the column
   forced a per-class switch in `init()` — where `sprite_asset` carries a
   `case ASSET_SOURCE_PROCEDURAL:` that exists only because the loop was copied
   from the mesh one. That dead case is the copy-paste drift, already visible at
   two classes, about to be six.
3. **The placeholder can fail to load.** `Missing` is
   `load_mesh("resources/obj/error.obj")`, and when that fails `get_mesh` falls
   back to a handle that is itself invalid — the "nothing drawn" symptom the
   ownership note in `asset.hpp` says cost an afternoon. A fallback that can
   fail is not a fallback.
4. **`import` is a one-file special case with three rules policing it** — an
   asset `.def` may not import, may not declare anything but asset classes,
   nothing but an asset `.def` may be imported. Three rules guarding a file that
   is a table.
5. **Sounds have no id space at all.** Ten-odd files hold raw path strings
   (`"resources/sounds/knife_hit1.wav"` in `flesh_impact.cpp`, `footstep.cpp`,
   `jump.cpp`, `land.cpp`, `rocket_explosion.cpp`, `hit_confirm_audio.cpp`,
   `play_state.cpp`). This is exactly what the enum exists to abolish, and it is
   the near-term driver.
6. **There is no packer.** The tree walk therefore has no natural owner and got
   squatted in `def_gen`'s resolve pass, then mirrored by hand in CMake. Every
   symptom above is downstream of that absent layer.

### The runtime half

Items 1-6 are all about how assets get *declared and provisioned*, which is what
the packer fixes. `asset.cpp` — how they get *loaded, cached and failed* — is a
separate inventory, and it has to move **first**, because the byte layer cannot
coexist with it:

7. **`resolve_mesh_path` is a four-candidate fuzzy resolver**
   (`asset.cpp:775-848`). Given `"pyramid"` it tries `pyramid`, `pyramid.obj`,
   `resources/obj/pyramid.obj`, `resources/obj/pyramid`, and reports through
   `printf` rather than `log_error`. `read_asset_bytes(path)` is one lookup
   against a manifest that already knows the answer; a probe in front of it
   reintroduces the exact runtime question the manifest abolishes.
8. **Cache keys are inconsistent across the five pools.** `load_skeleton` and
   `load_animation` use `canonical_cache_key()`; `load_mesh` keys on the
   resolved path; `load_texture` keys on the **raw string as given**
   (`asset.cpp:1064`). The comment at `asset.cpp:864` argues for canonicalising
   — *"two copies means two handles means bone 7 is no longer one bone"* — and
   then two of the five functions do not. `render_assets.cpp:16` adds a second
   raw-string texture cache on top of it.
9. **The `try_` convention is not applied to this API at all.** `load_mesh`,
   `load_texture`, `load_skeleton`, `load_animation`, `load_pbr_material`,
   `get_mesh`, `get_sprite` and both `find_*_in_cache` all fail by returning an
   invalid handle. No prefix, and `asset.hpp` contains **zero** `[[nodiscard]]`.
   The name promises the call cannot quietly fail and the return value says
   otherwise. See "Failure" below — the manifest is what makes this cheap to
   settle, and the answer is to delete the fallibility, not to add prefixes.
10. **`get_sprite` has no `Missing` fallback**, unlike `get_mesh`
    (`asset.cpp:1330`). `sprite_asset::Missing` is `ASSET_SOURCE_MISSING`, a
    hole that logs an error at every startup. The two hand-rolled bounds checks
    beside it are twins.
11. **`manifest_initialized` is a second, softer failure mode for a bug
    `state_for` already treats as fatal.** `get_mesh` before `init()` logs and
    returns `{}` — symptom: nothing drawn, which is the afternoon the ownership
    note in `asset.hpp` describes.
12. **`asset_test` does not cover the manifest.** 157 lines of `load_obj`,
    `load_texture`, path caching and a missing file. Nothing touches `init()`,
    `get_mesh` or the id-to-handle table — precisely the part steps 2 and 4
    rewrite.

## Core model

**One walk of the tree, owned by a packer. Everything else is downstream.**

```
resources/**  ──asset_pack──▶  generated/assets.manifest   (always)
                          └──▶  assets.pkg                 (ship builds)

assets.manifest ──def_gen──▶  assets_generated.{hpp,cpp}
                        └──▶  assets_bindings.cpp
```

- **`asset_pack` owns what exists on disk** — names, ids, bytes. A tool target
  beside `def_gen` and `map_convert`, and like them it is allowed project
  knowledge.
- **`def_gen` owns what C++ can say** — enums, storage, registration, and the
  binder that forces the loaders to exist. It keeps **zero** project knowledge:
  everything it needs arrives in the manifest.

The manifest and the package come from **one walk**, which is the load-bearing
property: the names and the bytes cannot disagree about what exists or what id
it has. That is what makes shipping safe, and no amount of `.def` polish buys
it.

## Classification

Two rules, and between them they classify the entire real tree correctly.

**1. Depth 1 only.** Files directly under `resources/<dir>/` are the id space.
Anything nested is the **path-referenced pool** — packed, but never enumerated.

That line is not arbitrary: it is exactly the existing split between "referenced
by id from code or a map" and "referenced by path from another asset."
`textures/harsh_bricks/albedo.png`, `models/textures/leet_skin.png` and
`blender/asset_library/**` are all below it already.

**2. Extension decides the class**, from one table in `asset_pack`:

```
.obj  .mesh   ->  mesh_asset_t
.png          ->  texture_asset_t
.wav          ->  sound_asset_t
.animation    ->  animation_asset_t
.hitboxes     ->  hitbox_asset_t
.ttf          ->  font_asset_t
```

An unknown extension at depth 1 is **an error naming the file**, not a skip —
that is what forces a loader to exist (see "The forced chain"). `README.md`,
`LICENSE.md`, `.mtl` and `.skeleton` are handled by an explicit ignore list in
the same table, so ignoring is a decision on the record rather than a
fallthrough.

The class name is the value type minus `_t`. This requires renaming
`animation_clip_t` → `animation_asset_t`; the convention is then total and the
table stays two columns.

**`.skeleton` is packed but NOT enumerated, and that is deliberate.** A `.mesh`
and an `.animation` each name their skeleton by **bare sibling name**, resolved
as `parent_path() / (name + ".skeleton")` inside `load_skinned_mesh` and
`load_animation`. Minting `skeleton_asset::Rig` on top of that gives one asset
two names — which is what an id space exists to abolish, and this is the single
case where two names means two loaded copies means bone 7 stops being one bone.
The sibling rule is the better identity here because it is the one the *file
format* uses; an id would be a second, weaker copy of it. So `skeleton_t` keeps
its name and `resources/models/rig.skeleton` stays in the path-referenced pool
despite sitting at depth 1.

**Directory names carry no meaning.** Merge `resources/obj` into
`resources/models`, or don't. Nothing regenerates differently. This is the
property the old scan list destroyed, and the reason `models/` holding four
kinds of file (`.mesh`, `.animation`, `.skeleton`, `.hitboxes`) was
unrepresentable before.

### Why not directory-as-class, and why not extension alone

Both fail on the real tree, and the failures are worth writing down so nobody
re-proposes them:

- **Directory alone** fails on `models/`, which holds four kinds.
- **Extension alone** fails on `.png`, which is a sprite in `sprites/` and a
  material map in `textures/harsh_bricks/`. Depth-1 is what resolves it, not the
  extension.

Sprites and textures are **unified** into one `texture_asset` class:
`sprite_handles` is already `asset_handle_t<texture_asset_t>`, so "sprite" was
never a distinct runtime type. `resources/textures/128x128.png` becomes a
texture id **once renamed** — a leading digit is not an identifier, see
"Minting" — and the two real texture sets are nested PBR folders that stay
path-referenced.

## Minting

Basename minus extension, case preserved. The result **must already be a valid
C++ identifier** — `[A-Za-z_][A-Za-z0-9_]*`. If it is not, that is an error
naming the file.

No mangling rule, deliberately. The minted name is the on-disk identity written
into `.source` map files (`reflection.cpp` writes `manifest[value].name`,
because **ids are not stable** — adding a file renumbers everything after it).
A mangling rule is a way for two files to quietly claim one name, and a map
saved yesterday must load today.

Known casualties today, all four to rename before step 2:

```
resources/fonts/Roboto-Medium.ttf      hyphen
resources/fonts/anwb-uu-regular.ttf    hyphens
resources/sounds/scout_fire-1.wav      hyphen
resources/textures/128x128.png         leading digit
```

Re-derive that list by walking depth 1 rather than trusting it — it is one
`grep -E '^[A-Za-z_][A-Za-z0-9_]*$'` over the stems, and `asset_pack` enforces
it on every build anyway.

Ids are positional, sorted by (class, logical path). Two files minting the same
name inside one class is an error naming both — it stays possible because
`.obj` and `.mesh` are deliberately one class, and the C++ compiler would catch
it anyway as a duplicate enumerator. Report it in `asset_pack`, where the
message can name the two paths.

**`.obj` and `.mesh` stay one class.** `Render.mesh` must be able to name either
a static prop or a skinned character; separate classes means two fields or a
variant. The difference is real, but it is a property of the *loaded asset* —
`is_skinned()` is `!skin.empty()`, and the renderer branches on it to pick
`mesh_skinned.vert` — not of its identity.

## Entry 0 is `Missing`, with no path

Every class gets `Missing = 0` with **no pkg entry**. Its bytes are a
compiled-in constant per class: a question-mark mesh, a magenta checker texture,
a zero-length sound.

This is the point of embedding it — `Missing` becomes infallible by
construction, which is the whole job of a placeholder (inventory item 3). It
also deletes the resolve pass's rule about skipping the placeholder's own file
during the scan, so the scan has no exceptions.

## What gets packed

Wider than the id space. `assets.pkg` holds **everything reachable at runtime**,
because a `.mesh` names its sibling skeleton and a material names its texture by
path:

- every id'd asset (depth 1, known extension)
- every nested file under a scanned directory (`models/textures/**`,
  `textures/harsh_bricks/**`)
- depth-1 files with an ignored extension that are still read at runtime:
  `.mtl`, named from inside an `.obj`, and `.skeleton`, named as a bare sibling
  from inside a `.mesh` or an `.animation`

Those last two are the whole reason the pool is wider than the id space, and
they are why an ignore list is a *decision on the record* rather than a
fallthrough: an extension can be ignored by the enumerator and still be
mandatory at runtime.

Excluded: `resources/blender/**` (source art) and `resources/shaders/**` (the
build compiles these to SPIR-V separately).

## `assets.manifest`

Generated text, in `src/shared/assets/generated/`, read only by `def_gen`.

```
# generated by asset_pack -- do not edit
class mesh_asset mesh_asset_t
  Missing     -
  Isosphere   obj/isosphere.obj
  Pyramid     obj/pyramid.obj
  Leet_Full   models/Leet_Full.mesh
class sound_asset sound_asset_t
  Missing     -
  Knife_Hit1  sounds/knife_hit1.wav
```

**It is deliberately not a `.def`.** In this project `.def` means hand-authored,
never generated, reviewed as a diff. A generated `.def` inverts that rule for
one file, and then "is this one I edit?" becomes a per-file fact to remember.
Not being a `.def` is also what deletes `import` and its three rules: `def_gen`
gains `--asset-manifest`, and the crossing stops being a special case.

Paths are relative to `resources/`, so the same string keys the loose backend,
the pkg index and the embed blob.

## What `def_gen` emits

Into `src/shared/assets/generated/`:

**`assets_generated.hpp` / `.cpp`**

- the class enums, `Missing = 0`
- `enum_traits<mesh_asset>` etc. — **currently not emitted for assets**;
  `emit_enum_traits` has exactly two call sites, entities (`def_gen.cpp:4550`)
  and cvars (`:5670`), and the event channel enums get theirs by another path.
  One call in `emit_assets_header`.
- `asset_info_t { name, path }` tables — **two columns**, `source_kind` deleted
- `to_string`, `try_from_string<T>`, `asset_class_manifest`
- **`asset_state_t` itself**, one pool and one `Enum_Array` per class:

```cpp
struct asset_state_t {
  Asset_Pool<mesh_asset_t>                             mesh_asset_pool;
  Enum_Array<mesh_asset, asset_handle_t<mesh_asset_t>> mesh_asset_handles;
  ...
};
```

`Enum_Array` replaces the raw `handles[COUNT]` arrays, and `try_get` replaces
the hand-rolled bounds check in `get_mesh` — CLAUDE.md already nominates it for
exactly this: *"the one for a key that came off the wire or out of a map file."*
Asset ids come off the wire.

**`assets_bindings.cpp`**

- `register_all(asset_state_t&)` — one generated loop per class
- one `decode_<class>` per class, switching on extension and calling
  `assets::load_<ext>` directly

`assets::init()` calls `register_all(state)` and nothing else.

**There is no per-class hand-written line anywhere.** That is the requirement:
storage is data-driven from the manifest, behavior is a named symbol. It is the
same split `entity_system_def.md` settled when `make_entity_pool` was deleted —
a hand-written registration call list is that switch, reincarnated, and must not
come back.

## The forced chain

Adding a new asset *kind* is impossible to get half-done:

1. drop `foo.ogg` into `resources/sounds/` → `asset_pack` errors: unknown
   extension `.ogg`
2. add the table row → the manifest carries it
3. `def_gen` emits `assets::load_ogg(...)` into the binder → **link error naming
   the symbol** until you write it

Two forced stops, both loud, neither skippable. This is the events pattern
exactly: *"there is no registry, no table and no bind step, so 'forgot to
register' is not representable — only 'forgot to write it'."*

## Runtime: two layers, one backend

```
id layer     mesh_asset::Pyramid  ->  "obj/pyramid.obj"
byte layer   read_asset_bytes("obj/pyramid.obj")  ->  Span<const uint8_t>
```

The byte layer serves **both** id-resolved assets and path-referenced ones, so
there is one call-site shape and "some from disk, some from memory" is
unrepresentable rather than merely discouraged.

```cpp
void                mount_asset_source();                 // fatal_error
Span<const uint8_t> read_asset_bytes(const char* path);   // fatal_error
bool                asset_exists(const char* path);
```

**No `try_` in the byte layer**, and that is the convention working, not an
exception to it. The manifest converts a runtime question into a build-time one:
it is not "we hope the file is there," it is "`asset_pack` saw it." A file
missing at runtime is a broken install or a stale exe — the middle row of the
failure table in CLAUDE.md, no recovery, `fatal_error`. Returning an optional
would make every call site write the same dead branch.

`asset_exists` is the one genuine probe — `load_pbr_material` treats a missing
`normal.png` as expected, and so does a `.mesh` material with no texture. It
takes no prefix because *"a `bool` that IS the answer is not a failure
channel."*

Three modes, **two implementations**:

| mode | blob from | when |
|---|---|---|
| `loose` | `fopen` under `resources/` | dev |
| `pkg` | mmap `assets.pkg` | ship |
| `embed` | the same blob in `.rodata` | single-exe ship |

**`loose` is about where the bytes come from, not about reloading them.** There
is no asset hot reload today and this design does not add one: all three cache
layers (the `Asset_Pool`, `render_assets.cpp`'s `g_mesh_by_asset`, the
renderer's own storage) are append-only, and `Asset_Pool`'s deque contract --
*"nothing removes from a pool"* — is exactly what makes the pointers `get()`
hands out safe. Reload is a real want (`animation_tool.cpp:752` says
re-exporting a clip you are watching needs a restart) and it is its own piece of
work, touching all three layers plus an invalidation story. `loose` is what
makes it *possible* later, not what delivers it.

`pkg` and `embed` differ only in where the base pointer comes from, which is why
`#embed` is not a third code path. **Clang 18.1.8 has no `#embed`** — verified
2026-08-21; `__has_embed` is undefined in both `-std=c23` and `-std=c++2c`. It
landed in clang 19, so `embed` mode is an LLVM upgrade away and blocks nothing
until then.

**The loose backend must own the bytes it hands out** — give it a
`std::deque<std::vector<uint8_t>>` scratch that lives until `assets::init()`
returns, then release it. All decoding happens during init, so that is natural,
and it is dev-only memory.

## Failure: the whole API, not just the byte layer

The manifest converts *"is this file there?"* from a runtime question into a
build question, and that answer propagates all the way up. Applied honestly to
CLAUDE.md's rule, **almost nothing in the asset system is fallible**, and the
`try_` prefix is wrong on almost all of it:

| call | fallible? | spelling |
|---|---|---|
| `read_asset_bytes(path)` | no — `asset_pack` saw the file | `fatal_error` |
| `load_obj` / `load_wav` / every decoder | no — a manifest file that will not parse is a broken install | `fatal_error` |
| `load_mesh(path)` and its four siblings | no | returns a valid handle, always |
| `get_mesh(id)` / `get_sound(id)` | no — an out-of-range id resolves to `Missing`, which cannot fail (see "Entry 0") | returns a valid handle, always |
| `asset_exists(path)`, `find_*_in_cache` | n/a — the answer IS the return | no prefix |
| `try_bake_font(bytes, sizes)` | **yes** | keeps `try_` |

The last row is the line, and it is worth stating as the rule: **the prefix
tracks whether the CALLER can cause the failure.** `try_bake_font` fails when
the pixel-height set the caller passed does not fit the atlas — that is a caller
parameter, so it stays fallible and keeps its prefix. Asset *presence* is not a
caller parameter, so nothing else here does.

This is a strengthening, not a relaxation. Today every one of those returns an
invalid handle on failure with no prefix and no `[[nodiscard]]` — the worst of
both, since the name promises the call cannot fail and the return value says
otherwise. The end state is that **an `asset_handle_t` handed out by this system
is always valid**; the only invalid ones left are default-constructed or from
the two cache probes, which is what makes `if (!handle.valid())` at a draw site
mean something specific again.

Two casualties fall out. `try_bake_font_from_file` goes away — its whole body is
the `fopen` that `read_asset_bytes` now owns, and `try_bake_font` keeps taking
bytes. And `compute_mesh_bounds(mesh, &min, &max) -> bool`, named in CLAUDE.md's
not-yet-converted list, becomes `compute_mesh_bounds(mesh) -> aabb_t`: an empty
mesh has an empty box, and that is an answer rather than a failure.

## The hand-written surface, forever

1. the value types (`sound_asset_t`)
2. one loader per extension —
   `sound_asset_t load_wav(Span<const uint8_t>, const char* debug_name)`
   — except `.wav`, where the loader hands the **encoded** bytes to miniaudio
   rather than decoding them itself; see step 1
3. one table row per extension, which is (2)'s signature written where the
   walker can see it

Row 3 is hand-written but **both its edges are enforced**: a file the table does
not cover is a build error, a row with no loader is a link error. The only
unenforced case is a stale row for a format no longer used — a warning. Its rows
are 1:1 with loaders you must write anyway, and it changes once per *format*,
never per file or directory.

## Migration

Each step is independently shippable. Do not bundle.

### Step 0 — the runtime half

`asset.cpp` cannot host the byte layer as written, so this lands first. It is
inventory items 7-12, it depends on nothing else in this document, and it is
what turns step 2's "diff the header" into a check that means something.

- **Delete `resolve_mesh_path`** (`asset.cpp:775-848`). Every path gets one
  spelling: relative to `resources/`, forward slashes, no candidate list. This
  is not optional cleanup — `read_asset_bytes` is a single lookup, and a
  four-candidate probe in front of it puts back the runtime question the
  manifest exists to answer at build time.
- **One cache key across all five pools.** Pick the manifest-relative path and
  use it in `load_mesh`, `load_texture`, `load_skeleton`, `load_animation` and
  `load_pbr_material` alike; the argument is already written at
  `asset.cpp:864` and two of the five do not follow it. Fold
  `render_assets.cpp`'s `g_texture_by_path` onto the same key while there — it
  is a second raw-string cache over the same textures.
- **Apply the failure table above**, with `[[nodiscard]]` on the one call that
  keeps a prefix. `asset.hpp` has zero today.
- **`get_sprite` gets `get_mesh`'s `Missing` fallback**, and both hand-rolled
  bounds checks become `Enum_Array::try_get`.
- **`manifest_initialized` becomes `fatal_error`**, matching `state_for`
  directly above it. A `get_mesh` before `init()` is a broken build, and
  returning `{}` makes its symptom "nothing drawn".
- **`asset_test` gets manifest coverage**: `init()`, id-to-handle for every
  class, and the `Missing` fallback. Today it covers only the layer below.

**Done when:** no path in the asset system has more than one spelling, every
accessor returns a valid handle or dies, and `asset_test` fails if the manifest
half regresses.

> **LANDED 2026-08-22.** All six bullets. Three things worth knowing that the
> plan above did not say:
>
> - **`emit_enum_traits` for asset classes was pulled forward from step 2**,
>   because `Enum_Array<mesh_asset, ...>` needs `enum_traits<mesh_asset>::count`
>   and step 0 is what introduces the Enum_Array. Asset classes are
>   `DECLARATION_ASSETS`, not `DECLARATION_ENUM`, so `emit_enum_traits` does not
>   reach them — it is a short loop of its own at the end of
>   `emit_assets_header`. Step 2's "diff the header" check is cleaner for it,
>   not muddier.
> - **The fatal refusals cost two negative tests their subject.** A stale
>   skeleton hash used to come back as an invalid handle and
>   `test_model_format` asserted on it; a `fatal_error` cannot be caught
>   in-process. Both cases now assert the DISAGREEMENT the refusal keys on — the
>   parsed hash against the sibling skeleton's — which is the data, not the
>   policy. Coverage of "the loader refuses" is genuinely gone; a subprocess
>   harness is the only way to get it back and no other test wants one.
>   `asset_test`'s `test_invalid_path` went the same way and is replaced by
>   `test_asset_exists`.
> - **`get_sprite` has the shape but not yet the promise.** It is `try_get` plus
>   a `Missing` fallback exactly like `get_mesh`, but `sprite_asset::Missing`
>   has no source file, so that fallback is itself invalid and `init()` logs it
>   at every startup. This is the one remaining invalid handle the system can
>   hand out, and step 4 is what closes it.
>
> `load_pbr_material` and `resolve_surface_mesh` gained `asset_exists` probes,
> which is the byte layer's one genuine probe arriving a step early — step 1
> re-points its body and no call site moves. `compute_mesh_bounds` returns
> `shared::aabb_bounds_t` (the min/max query form its callers were already
> unpacking into) rather than the `aabb_t` written under "Failure".

### Step 1 — bytes-based loaders, `loose` only

Convert every decoder to take bytes. Nothing else changes; `assets.def` still
drives everything and behavior is identical.

- `load_obj`, `parse_mesh_file`, `parse_skeleton_file`, `parse_animation_file`
- the two **derived sibling paths**, which are the awkward part and the reason
  they are called out separately from the decoders above: an `.obj` names its
  `.mtl` from *inside the parser* (`asset.cpp:244` derives
  `obj_dir + mtl_filename`), and a `.mesh` or `.animation` names its
  `.skeleton` from `parent_path() / (name + ".skeleton")`. Both are
  ignored-extension files that are nonetheless fetched at runtime, and both
  derivations have to produce a `resources/`-relative key for
  `read_asset_bytes` like everything else.
- `stbi_load` → `stbi_load_from_memory`
- stb_truetype already takes memory; delete `try_bake_font_from_file` and pass
  `read_asset_bytes(path)` into `try_bake_font` at the one call site
  (`client_impl.cpp:98`)
- **audio — not a one-line swap, and `ma_decoder_init_memory` is the wrong
  handle to pull.** `play_3d` / `play_2d` go through `ma_sound_init_from_file`
  (`audio_system.cpp:173`), which means miniaudio's own **resource manager** is
  doing the caching, the decode and the ref-counting today. Reaching for
  `ma_decoder_init_memory` throws all three away and turns the voice pool into
  our problem. The fit is
  `ma_resource_manager_register_encoded_data(rm, path, bytes, size)` at init:
  miniaudio then serves that same key from memory, and neither the voice pool
  nor a single call site moves. That is also why `load_wav` does not decode —
  registration *is* the loader, and there is no `sound_asset_t` full of PCM to
  own (see step 5).

Add `read_asset_bytes` / `asset_exists` / `mount_asset_source` with the loose
backend behind them. This is the wide mechanical change, and it is worth having
landed and tested before anything structural moves.

**Done when:** the game runs identically and no decoder opens a file.

> **LANDED 2026-08-22.** Every bullet. `mount_asset_source` / `read_asset_bytes`
> are in `asset.hpp` beside `asset_exists`, and their state (`asset_source_t`)
> is a member of `asset_state_t` rather than a static of its own — a per-module
> copy would be mounted once and empty everywhere else, which is the bug the
> ownership note in that header already describes. The three launchers mount
> between `set_state` and `init`; loose mode checks `resources/` is reachable,
> which turns "launched from the wrong directory" into one message instead of a
> fatal on whichever asset happened to load first.
>
> Five things the plan above did not say:
>
> - **A span from `read_asset_bytes` is valid for the PROCESS LIFETIME**, not
>   until `init()` returns, and the blobs are keyed by `asset_cache_key` rather
>   than piled into an anonymous arena. Two reasons, both discovered by writing
>   it: loads NEST (an `.obj` is mid-walk while its `.mtl` and its textures are
>   read), so a scratch released at a known point is a dangling read rather than
>   a saving; and `ma_resource_manager_register_encoded_data` **does not copy**,
>   so the sound bytes must outlive the engine. `pkg` and `embed` have that
>   lifetime for free — retaining in `loose` is what makes the three modes agree
>   rather than what makes it the odd one.
> - **Audio registers LAZILY, on first play, not at init**, because there is no
>   sound manifest until step 5 and the alternative was a runtime directory
>   scan — exactly the question the manifest exists to answer at build time. It
>   is one `try_register_sound` keyed by path in `try_start_voice`; not one call
>   site moved, and the voice pool never learned about it. It keeps an
>   `asset_exists` probe, and that probe is the only line in the file that step 5
>   deletes: `footstep.wav` and `rocket_fire.wav` are *referenced and absent*
>   today, so a hardcoded sound name is still a name that can be wrong. Verified
>   against miniaudio directly rather than assumed: a name registered with bytes
>   and **no file behind it** loads.
> - **`models::parse_*_file` lost the `_file`** — they are `parse_skeleton`,
>   `parse_mesh`, `parse_animation` and `try_parse_hitbox_rig`, each taking
>   `(Span<const uint8_t>, const char* debug_name, ...)`. `line_reader_t` grew
>   the blob cursor, so `next_line` needs no stream. Both line splitters strip a
>   trailing `\r`: the `ifstream` they replaced was opened in TEXT mode and did
>   that for them, and a `\r` left on the end rides into the line's last token.
> - **The malformed fixtures stopped being files.** `test_model_format` and
>   `hitbox_rig_test` wrote a dozen temp files purely to have a path to hand a
>   parser; they are now string literals, and `debug_name` is what the error
>   messages say. `asset_test`'s two fixtures still have to be *written*, so they
>   moved from `%TEMP%` to `cmake_build/asset_test_fixtures/` — an absolute path
>   would still open, which is exactly why the fixtures move rather than the
>   one-spelling rule bending for the test that checks it. `asset_test` gained
>   `test_read_asset_bytes` (the bytes are the file; two spellings are one blob).
> - **A missing `.mtl` is fatal now**, where it used to warn and draw untextured.
>   An `.obj` names its `.mtl` from inside itself, so it is a derived sibling
>   like a `.mesh`'s `.skeleton`, not a caller parameter — same rule, same
>   consequence.
>
> Two things came along because they were the same decision one line away:
> `load_texture` was still keying its pool on the raw argument rather than the
> cache key (step 0's rule, missed in one of five pools), and `renderer.cpp`'s
> `create_particle_texture` was the last `stbi_load` in the tree.

### Step 2 — `asset_pack` replaces `assets.def`

- new tool target `src/tools/asset_pack.cpp`: walk, classify, mint, emit
  `assets.manifest`. Write-if-different.
- `def_gen`: add `--asset-manifest`; delete `parse_assets_body`, the `assets`
  declaration kind, `import` and its three rules, `--asset-root`, and
  `asset_source_kind_t`
- `def_gen`: call `emit_enum_traits` for asset classes
- emit `asset_state_t`, `register_all` and `assets_bindings.cpp`
- CMake: delete the `file(GLOB ... CONFIGURE_DEPENDS)`; `asset_pack` runs every
  build behind a stamp, and `def_gen` depends on the single manifest file
- rename `animation_clip_t` → `animation_asset_t`, `sprite_asset` →
  `texture_asset` (`skeleton_t` keeps its name — it is not a class, see
  "Classification")
- rename the four un-mintable files listed under "Minting"

**Done when:** two checks, and both are needed. `assets_generated.hpp` diffs
against the old one only by the `sprite_asset` → `texture_asset` rename and the
new classes — diff it, do not eyeball it. And `asset_test` still passes, which
is only worth anything because step 0 taught it about the manifest: the header
diff is a check on emitted *text*, and step 2 rewrites `asset_state_t`,
`register_all` and every id-to-handle lookup underneath it.

> **LANDED 2026-08-22.** Every bullet, plus step 3 and the mesh/texture half of
> step 4, both of which turned out to be forced rather than optional (see
> below). All 31 tests pass and the game runs.
>
> Nine things the plan above did not say:
>
> - **STEP 3 IS NOT SEPARABLE FROM STEP 2, and it is not close.** Deleting
>   `asset_source_kind_t` deletes the `procedural` column, which deletes
>   `mesh_asset::Box` and `mesh_asset::Sphere` — and `physics_body_system.cpp`
>   names both. Step 2 does not compile without the bake, so the bake happened
>   here: a one-shot that runs the two generators verbatim and dumps `.mesh`,
>   after which all six generators and `generate_mesh_for_key` are unreferenced
>   and gone. `asset_test` asserts both baked meshes are unit-sized and centred,
>   which is the guard step 3 asked for. The other four primitives (`Arrow`,
>   `Cone`, `Wedge`, `Cylinder`) were referenced by nothing and were simply
>   deleted. **Step 3 is complete.**
> - **A static `.mesh` still writes the skin fields.** `parse_mesh` reads four
>   bone indices and four weights on every vertex line and only *interprets*
>   them when a `skeleton` line was present. The first bake omitted them and
>   died on line 4. It also has to write `scale 39.37`, which parse_mesh checks
>   against `METRES_TO_UNITS` — it records that the two sides agree about how
>   big a metre is, and applies nothing.
> - **`asset_state_t` needs TWO generated headers, not one.** `animation.hpp`
>   includes `entities_generated.hpp`, which includes `assets_generated.hpp` —
>   so emitting the state into the header entities include is a cycle.
>   `assets_generated.hpp` stayed the light **id space** and
>   `asset_state_generated.hpp` is the **storage**, pulling in each class's value
>   header. `hitbox_rig.hpp` also had to stop including `asset.hpp` (it wanted
>   `mesh_asset_t`, which now lives in the new hand-written `asset_types.hpp`).
> - **The manifest carries FOUR columns, not two.** `class <name> <value-type>
>   <header> <extension>...`. The header column is what keeps def_gen free of
>   project knowledge once it emits the state; the extension list is a genuine
>   correction, below.
> - **A class's decoder set is a property of the CLASS, not of what is on
>   disk.** Deriving it from the entries was the first thing written and it was
>   wrong: `load_<class>` also serves **path-referenced** files that were never
>   enumerated, so a format would stop being loadable the day the last file of
>   it left the tree. `asset_test`'s `.tga` fixture found this within a minute.
>   `.tga` is now in the class table beside `.png`, and `decode_png` /
>   `decode_tga` are one function behind two symbols — stb_image sniffs the
>   format out of the bytes, but the extension set is what a new format has to
>   reach.
> - **Two classes are named for their value type rather than the reverse.**
>   `hitbox_rig_t` is a domain type `hitscan` reads; renaming it to
>   `hitbox_asset_t` to satisfy "class name is the value type minus `_t`" would
>   be the tail wagging the dog, so the class is `hitbox_rig`. The accessors
>   drop a trailing `_asset` (`load_mesh`, `get_texture`), which is what let
>   every existing call site survive — `get_sprite` → `get_texture` is the one
>   rename.
> - **`sound_asset_t` is a path and `font_asset_t` is the bytes.** Neither
>   decodes. miniaudio's resource manager already owns sound samples and a
>   second copy would be a second answer to "is this loaded"; a font's pixel
>   height is a call-site parameter and the atlas it bakes into is the client's.
>   This is what let `.wav` and `.ttf` become classes now, in `game_shared`,
>   rather than waiting for the client-side steps.
> - **Four files renamed for mintability, and three more for CASE.** The plan
>   listed the four (`Roboto-Medium.ttf`, `anwb-uu-regular.ttf`,
>   `scout_fire-1.wav`, `128x128.png`). It missed that the old generator
>   CAPITALISED minted names (`pyramid.obj` → `Pyramid`) and the new rule
>   preserves case — so `pyramid.obj`, `isosphere.obj`, `smoke.png` and
>   `error.obj` were renamed to their minted names rather than the code being
>   renamed to lowercase. That is the no-mangling rule doing its job: if you
>   want the id spelled `Pyramid`, the file is `Pyramid.obj`.
> - **The suite was taking four minutes, and it was a MODAL DIALOG.** On Windows
>   both `abort()` and a failed `assert()` open a blocking box, so a failing test
>   under ctest is a hung one waiting for a click. `log.hpp` now disables both at
>   static-init time (an inline variable in the header, not a TU in `game_shared`
>   — a static initializer there gets linker-dropped, which this codebase has
>   already been bitten by). Full suite: 258s → **2.0s**.
>
> `assets.def`, `parse_assets_body`, the `assets` declaration kind, `import` and
> its three rules, `--asset-root`, `asset_source_kind_t`, `expand_asset_manifests`,
> `derive_asset_name`, `check_asset_family`, `DEF_FAMILY_ASSET`, `load_import`,
> the `file(GLOB ... CONFIGURE_DEPENDS)` mirror, `generate_mesh_for_key` and the
> six generators are all gone.

### Step 3 — delete `procedural` — **LANDED with step 2**, see above

`Arrow`, `Cone`, `Wedge` and `Cylinder` are referenced by nothing outside the
generated table. Delete them.

For `Box` and `Sphere` — the only two with a caller
(`physics_body_system.cpp`, both scaled through `render.scale`) — **no bake tool
is needed.** Run the existing generator once, dump `.mesh` (engine units, so the
`load_obj` 100-unit normalization is not in the way), commit the two files, and
delete all six generators plus `generate_mesh_for_key`.

Size is a transform, not an asset parameter. Debug geometry with a size is
`debug_draw_list_t::arrow/box/wire_sphere/wire_capsule`, which already exists
and is not the asset system's business. Runtime-generated geometry that
genuinely differs in topology is `register_dynamic_mesh`, which also already
exists.

The one thing to guard: `physics_body_system.cpp` scales both meshes through
`render.scale` on the assumption that a generated primitive is **unit-sized**,
and nothing asserts that today, so the check is visual. Dumping raw generator
output to `.mesh` preserves it — a `.mesh` is engine units and skips the
normalization — but assert the baked bounds in `asset_test` rather than trusting
the export, because the failure mode is a physics body 100x too large and it
will not be obvious which of the two regimes drifted.

### Step 4 — embedded `Missing` — **LANDED with step 2**

One compiled-in constant per class at id 0. Deletes the last way a placeholder
can fail.

> **LANDED 2026-08-22.** In full, and earlier than planned because the
> alternative was a regression: step 2 makes `Missing` path-less, and
> `Render.mesh` defaults to `.Missing`, so shipping step 2 alone would have made
> every unassigned mesh draw *nothing* until step 4. Since `register_all` is
> generated it needs one uniform rule anyway, so all six classes got a
> `make_missing_<class>()` — a question-mark mesh built out of boxes, a magenta
> checker texture, and four empty values that only have to be valid.
> `sprite_asset::Missing`, the one invalid handle the system could still hand
> out, is closed. `resources/obj/Error.obj` is still on disk and is now an
> ordinary asset (`mesh_asset::Error`).

### Step 5 — `sound_asset` at the call sites — **LANDED 2026-08-23**

`get_sound(id)` and `play_3d(sound_asset, ...)` replaced the `const char*`
overloads **outright**, not alongside — a path at a call site is exactly what
the enum exists to abolish. Every one of the ten-odd files in inventory item 5
is converted and there is no path-taking overload left to fall back to.

**miniaudio keeps owning the PCM**, and that is what made this step small.
`sound_asset_t` is not a buffer of samples: the bytes are registered with the
resource manager under their manifest path, so `get_sound(id)` resolves an id to
that path and `play_3d` hands it to the `ma_sound_init_from_file` call that was
already there. An `Asset_Pool<sound_asset_t>` of decoded audio beside
miniaudio's own cache would be a second copy of every sound in memory, and a
second answer to "is this loaded".

Three things fell out that the original sketch did not name:

**Registration became EAGER, and the `asset_exists` probe is gone.** It used to
be lazy-on-first-play behind a probe, because a hardcoded string could name a
file that was not there. An id cannot: `assets::init()` has already loaded every
`sound_asset` and every launcher runs it before `client::init`, so
`audio_system_t::init` walks the closed enum once and hands the resource manager
each blob. `read_asset_bytes` is fatal on a miss, which is correct — a
manifest-enumerated file that is absent at runtime is a broken install.

**`Missing` is how a CONTENT GAP is written down.** `footstep.wav` and
`rocket_fire.wav` never existed, and with ids there is no name left to misspell
— so `on_footstep` plays `sound_asset::Missing` and the rocket launcher's row in
`WEAPON_FIRE_SOUNDS` holds `Missing`. That is a declared absence at the site
that has it, not a silent no-op: `try_start_voice` logs once per id when the
path behind it is empty. The complain-once set of path strings became an
`Enum_Array<sound_asset, bool>` for the same reason the id space is closed.

**`fire_sound_for` became `try_fire_sound_for`.** It returns
`std::optional<sound_asset>` rather than a nullable pointer, which is the
convention working: `last_fire_weapon` comes off the wire with no range check,
so the caller is what makes the call fallible.

### Step 6 — `pkg` and `embed` — **LANDED 2026-08-23**

`#embed` is in clang 19+ (this build is 22.1.7), and it is fast enough to be a
non-event: 20MB embeds in 0.67s, the real 58MB package in about the same.

**One format, one reader, two ways to get the bytes** — `asset_package.hpp` is
the whole of it, and the grammar is in its header comment. A package is header,
index, string table, blob; entries are sorted by path so a lookup is a binary
search straight over the mapped range, with nothing parsed at mount and no map
built. Entries are read out with `memcpy` rather than by pointing a struct at
the bytes, which buys the alignment question never being asked of a `#embed`ed
array. The **same TU compiles into `asset_pack` and into `game_shared`**, so the
writer and the reader are not two descriptions of one format.

**`--package` is the SAME walk as `--manifest`, not a second tool.** That is the
one-walk rule reaching its actual conclusion: `walk_directory` became one
recursive traversal producing both outputs, so the files it minted ids for are
the same objects it put bytes in. Depth 1 is still the id space; everything
nested is packed and never enumerated. `.mtl` and `.skeleton` are packed exactly
as this document said they must be (they are named from inside another asset and
are mandatory at runtime), and `UNPACKED_EXTENSIONS` — `.md` today — is the
narrowing decision on the record beside `IGNORED_EXTENSIONS`. The real tree
packs to 59 files, 57.8MB.

**The mode is a build-time choice**, `-DTILDE_ASSET_SOURCE=loose|pkg|embed`,
defaulting to loose. Not a runtime switch: a shipped exe has exactly one answer,
and a flag would be one more way to launch a build that cannot find its assets.
`assets.pkg` is built unconditionally so `pkg` is one flag away and never a
stale artifact.

Verified end to end: a dedicated server built with `embed`, copied to an empty
directory with nothing but its DLL and `maps/`, loads every mesh, texture,
sound, animation, font and hitbox rig — including the `.mtl` and `.skeleton`
siblings resolved from inside other assets — out of `.rodata`. `asset_test`
round-trips the format (sort order, an empty asset, data alignment, a prefix
that must not match, and three refusals) and its five fixture-backed tests are
`#if`'d out of the packaged modes, because "write a file and then load it" is a
loose-mode question by construction.

Two things left open on purpose:

- **Shaders are still outside the package.** `resources/shaders/**` compiles to
  SPIR-V into the build directory on a path of its own, so a packaged build
  still reads those from disk. Folding them in is a shader-pipeline change, not
  an asset-pipeline one.
- **`embed` grows every exe that links `game_shared`**, tests included, because
  that is where the byte layer lives. It is a ship mode, not a dev mode, and
  that is the whole reason the TU is compiled only when the option asks for it.

## What dies## What dies

`src/shared/assets/assets.def` · `parse_assets_body` (~130 lines) · the `assets`
declaration kind · `import` and its three validation rules · `placeholder` /
`scan` / `procedural` grammar · `asset_source_kind_t` and its column · the
per-class `switch` in `assets::init()` including the dead
`case ASSET_SOURCE_PROCEDURAL` · `generate_mesh_for_key` and six `strcmp`s ·
six primitive generators · `file(GLOB ... CONFIGURE_DEPENDS)` in CMakeLists ·
`--asset-root` · the raw `handles[COUNT]` arrays and their hand-rolled bounds
checks · `sprite_asset` as a distinct concept · ten-odd hardcoded
`"resources/sounds/*.wav"` strings · `resolve_mesh_path` and its four-candidate
list · three different cache-key rules across five pools ·
`render_assets.cpp`'s `g_texture_by_path` · `try_bake_font_from_file` ·
`manifest_initialized` as a soft failure · every invalid handle this system
could hand out.

## Open decisions

- **Table location.** Started inside `asset_pack`'s source: one fewer file, one
  fewer parser, and a wrong type name still fails at compile time in the
  generated binder. If it ever wants to live next to the loaders instead, an
  eight-row data file both read is the alternative.
- **`.hitboxes` as a class.** There is exactly one (`rig.hitboxes`). Cheap to
  include and it removes another path string; skip it if a class for one file
  feels wrong.
- **`load_obj`'s 100-unit normalization** stays for now. Step 3 routes around it
  by baking to `.mesh`. Deleting it changes how every mesh in the game is sized
  and is its own piece of work.
- **Asset hot reload** is not in this design and is not blocked by it. It needs
  an invalidation story across all three cache layers and a rule for the
  pointers `Asset_Pool::get` hands out; `loose` mode is the precondition, not
  the feature. Worth doing when the animation tool's restart-to-see-a-clip loop
  gets annoying enough.
