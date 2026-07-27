# THE ENTITY TRACK — completed phases

Phase order and rationale live in `todo.md`; design rationale in `entity_def.md`.
This file keeps the finished phases so `todo.md` stays a work list.

## P0 — pascal_string_t::set() tail residue bug  ✅ DONE

`set()` didn't zero `data[length..N)` or re-terminate. Shrinking `"hello"` →
`"hi"` left `"hillo"` from `c_str()` (which assumes zero-init termination), and
logically-equal strings could memcmp-unequal → phantom deltas in baseline
diffing. This established the canonical-zero-padding invariant that both the
generator's string model (`entity_def.md`, Strings) and P2's binary field
diffing assume.

- [x] `network_types.hpp` `pascal_string_t::set()`: zero `data[length..N)` after copy
- [x] While there: silent truncation at capacity violated no-silent-failures —
      `set()` now returns bool and asserts; added `clear()`
- [x] Storage widened to `data[N + 1]` so a full-capacity string is still
      null-terminated — `c_str()` on an exactly-N-char string used to read one
      byte past the buffer, and the old "always null-terminated" comment was
      simply false in that case. N is still the usable capacity.
- [x] **Same invariant was violated on the wire path** (`entity.cpp`,
      `deserialize_field_from_bits` PascalString branch): it wrote `length`
      chars and terminated only when `length < max_length()`, never zeroing the
      tail — so a shorter string deserialized over a longer one left residue and
      every later baseline memcmp reported a phantom delta. That is the exact
      bug this phase existed to kill, on the path that matters most. Now memsets
      the tail.
- [x] While there: that branch trusted the untrusted uint8 wire length without
      clamping to capacity, so a corrupt or hostile packet claiming 255 chars
      wrote up to 5 bytes past a 250-capacity field. Now clamps, still consumes
      every announced byte (or the bitstream desyncs for all later fields), and
      logs loudly on overflow.
- Verified: full build green; `test_entity_delta_packing`, `session_test`,
  `ecs_test`, `entity_layout_test`, `transaction_system_test`,
  `map_migration_test` all pass. `network_test` still segfaults exactly as
  before (pre-existing, see Known failing tests in `todo.md`) — unchanged by
  this work.

---

## P1 — Geometry exit (out of the entity system, into the map module)  ✅ DONE
*landed on branch `p1-geometry-exit`. Full build green; `session_test`,
`transaction_system_test`, `map_migration_test`, `ecs_test`,
`entity_layout_test`, `test_entity_delta_packing`, `navmesh_test`,
`server_loop_test` and the rest pass. `asset_test` and `network_test` still fail
exactly as before (pre-existing — see "Known failing tests" in `todo.md`).*

AABB / Wedge / StaticMesh / Displacement became plain map-owned C++ value
types. They are never networked, so they were paying the schema system's
blittable/fixed-size/memcmp constraints for nothing — and the `[64]f32` heights
cap was the format's limitation leaking into what the game can express.

```cpp
struct displacement_t
{
  transform_t transform;
  material_id material;
  u32 resolution;
  std::vector<f32> heights;   // assert(heights.size() == resolution * resolution)
};
```

**This phase also disarmed half the ownership bug for free.** The static path in
`init_session_from_map` (`game_session.cpp:10`) was selected by
`is_collision_geometry()` and did `static_entities.push_back(entry.entity)` —
copying the *shared_ptr*, so session and map aliased the same object. Deleting
the branch made that aliasing (and its lifetime coupling, and the
writeback-into-map hazard) stop existing, shrinking P7's scope before P7 starts.

- [x] Geometry value types in `shared/map_geometry.{hpp,cpp}`: `box_geometry_t`,
      `static_mesh_geometry_t`, `displacement_geometry_t` in a
      `std::variant` (`geometry_value_t`), sharing a `geometry_surface_t`.
      `std::vector<vec3> displacements` — no cap, no erasure. Displacement math
      + `generate_displacement_mesh` moved off the entity.
- [x] Handwritten map save/load per kind (`serialize_geometry` /
      `parse_geometry`). Three kinds, not four — wedges were already retired
      before this phase and did not come back. Keys emit in **declaration
      order** (P5's intent, taken here for free) so the file is git-diffable.
- [x] Map text I/O rewritten around a generic block parser with the grammar in
      a header comment: `block := keyword '{' property* '}'`, keyword ∈
      `entity | box | static_mesh | displacement`. Unknown keywords are skipped
      and reported rather than derailing the parse.
- [x] One-time map file conversion (`convert_legacy_geometry_entity`), plus
      `src/tools/map_convert.cpp` so it can be run deliberately over every map
      with a report instead of one-at-a-time by opening each in the editor.
      Killed the `"center"` / `"half_extents"` compat shims by reading them
      **here, once** — `maps/test` turned out to be older than expected (flat
      `"half_extents"`, not the `"volume"` blob), which the rewritten
      `map_migration_test` caught.
      `maps/new_map.source` + `maps/other.source` converted (`.preconvert.bak`
      alongside); `maps/test` deliberately left legacy — it's the test fixture.
- [x] Session init: `is_collision_geometry()` gone from the routing.
      `game_session_t::static_entities` replaced by
      `std::vector<map_geometry_t> geometry`, a **copy** of the map's list —
      `session_test` now asserts the non-aliasing directly (write the session's
      copy, check the map didn't change).
- [x] Editor seam, uid-keyed across both regimes: `map_t::has_object` /
      `remove_object` / `object_count`, and free functions
      `compute_object_bounds`, `get_object_position` / `set_object_position`,
      `get_object_box` / `set_object_box`, `collect_object_bounds`. The tools
      call those and mostly don't branch on regime. (Uniform editing never
      actually required schemas — confirmed.)
- [x] Handwritten inspector panels in `editor/geometry_editor.cpp` (~80 lines of
      ImGui for all three kinds), with widgets that suit each kind: a named
      `active_face` dropdown, and a subdivision slider that **resamples** the
      grid instead of flattening it.
- [x] Transaction system gained the geometry **value-swap** flavor
      (`diff_geometry_created/removed/modified_t` + `geometry_values_equal`).
      Bit-exact, so unlike the entity flavor it cannot lose a change too small
      to survive `%.6f` — `transaction_system_test` asserts exactly that.
- [x] `build_editor_bvh()` now builds over BOTH lists; `Collision_Id.index` is
      an object uid resolved through the seam, so a pick doesn't know or care
      which regime it hit.
- [x] **Batch transactions** — multi-object delete and the multi-object Ctrl+drag
      each push ONE transaction now (`Selection_Tool::commit_drag_snapshots`, and
      the delete handler), and `transaction_system_test::test_mixed_batch_delete`
      covers a batch spanning both regimes. The transaction builder already
      supported this; the tools just weren't using it. (Originally listed under
      P2.)
- [x] Answers the loose note "why is AABB a schema? it's not a good decision" — it stopped being one here.

**Bugs found and fixed while in here** (all pre-existing, all in code this phase
rewrote anyway):
- `displacement_tool`'s `commit_select_edit()` only *dropped* its snapshot and
  never pushed a transaction, so Select-mode Q/E height steps were silently not
  undoable. Now commits as one value swap per run of steps.
- The subdivision slider called `init_displacement()`, which zeroed the grid —
  changing subdivision threw away the sculpt. Now `resize_grid_preserving()`.
- `set_displacement`'s bounds check used `idx + 2 >= count` on a flat float
  array, i.e. it rejected the last vertex of a correctly-sized grid. Gone with
  the flat array.
- The game regenerated every displacement's mesh **every frame** (the editor
  cached it); both now go through one cached path in
  `client/geometry_renderer.cpp`.

**Deliberately NOT changed** (each would be a gameplay/scope decision, not part
of moving geometry out of the entity system) — the live consequences of these
two are tracked in `todo.md`:
- Displacements still aren't registered as Jolt static bodies, exactly as
  before. Player movement (BVH) collides with a displacement's box bound but
  projectiles pass through. The real fix is heightmap collision — see the
  TODO in `get_collision_planes`.
- `box`/`displacement` lost their `orientation` field. It was a lie: only the
  draw call read it, so a rotated one rendered rotated and collided unrotated.
  `static_mesh` keeps it.

---

## P2 — Editor undo: string-map snapshots → binary field diffs  ✅ DONE
*Full build green. `transaction_system_test` (11 subtests, 3 new), `session_test`,
`ecs_test`, `entity_layout_test`, `test_entity_delta_packing`,
`map_migration_test`, `navmesh_test`, `server_loop_test`, `udp_socket_test` all
pass. `network_test` (access violation) and `asset_test` (exit 3) fail exactly as
before — see "Known failing tests" in `todo.md`.*

Undo snapshotted entities as `std::map<string,string>` via `get_all_properties()`
and detected change by STRING comparison (`diff_properties`). Wasteful (double
text round-trip + map alloc per edit) and it had a real bug: comparing
`%.6f`-formatted text means a sub-threshold change produces NO diff and the edit
silently vanishes from the stack. Both flavors are binary now, and the entity
flavor makes the same guarantee the geometry one already did.

**The plan said clone via `serialize(writer, nullptr)` → deserialize. That is
wrong and was not done.** `write_coord` quantizes floats to a 5-bit fraction
(~1/32), so a bitstream round trip is LOSSY — snapshotting that way would snap
every entity position on undo, which is a worse version of the bug the phase
exists to fix. `clone_entity` copies schema field bytes instead: exact, no
bitstream, and it is the same representation `capture_field_changes` diffs.
Field-by-field rather than one `memcpy(sizeof(T))` because the source is only an
`Entity*` there, and a whole-object copy would also stomp the clone's vtable
pointer.

- [x] `shared::clone_entity(const Entity*)` (`entity.cpp`) — exact byte copy of
      every schema field into a fresh instance of the same concrete type.
      Returns `shared_ptr`, not the planned `unique_ptr`: `map_t` stores
      `shared_ptr` and the factory returns one, so unique would only add a
      conversion. Ownership is P7's question anyway.
- [x] `diff_reversible` → `capture_field_changes` (`schema.hpp`). No deprecated
      alias — the plan assumed networking callers, but it turned out to have had
      **no callers at all**, so it was a clean rename.
- [x] New `network::write_field_changes(target, changes, schema, write_new_value)`
      — the apply/revert counterpart. Undo and redo differ only in which side of
      the change is written, so it's one function, not two mirrored ones. Returns
      false and logs on an unknown field index or a size mismatch rather than
      writing a wrong-sized value or skipping quietly.
- [x] `transaction_system.hpp`: `property_change_t` and `diff_properties` deleted.
      `diff_entity_modified_t` is now `{uid, entity_type, vector<field_change_t>}`
      — it carries the **enum tag**, not the planned classname string: field
      indices are per-class, so apply/revert verifies the entity still at that uid
      is the same type before writing bytes into it, and the closed enum is the
      cheaper and safer way to ask.
- [x] created/removed diffs store a whole cloned entity (`entity_snapshot_t` =
      `shared_ptr<const Entity>`) rather than the planned serialized blob — same
      "binary everywhere" intent, exact instead of quantized, and it makes the
      two flavors symmetric (entity clone ↔ geometry value). Restore re-clones,
      so redoing a delete twice can't hand the map an object the undo stack still
      owns.
- [x] Tool snapshots migrated off `map<string,string>`:
      `sculpting_tool::sculpt_start_entity`,
      `selection_tool::object_snapshot_t::entity`, `editor_gizmo::start_entity`.
      `displacement_tool` was already fully geometry-side.
      `add_modified_from_diff(uid, before_snapshot, live_entity)` is now the
      entity mirror of `add_geometry_modified` — same shape at every call site.
- [x] `placement_tool` entity duplication: `create_entity_by_type` +
      `init_from_map(get_all_properties())` → one `clone_entity` call, matching
      the geometry branch directly above it.
- [x] `get_all_properties`/`init_from_map`/`parse_string_to_field`/
      `serialize_field_to_string` left in place — still used by map file save/load
      until P5. Verified they now appear ONLY at that disk boundary (`map.cpp`)
      and `entity.cpp`'s own definition: zero references left anywhere under
      `src/client/`.
- [x] `test_transaction_system.cpp` on the binary API, plus three new subtests:
      * `test_modify_thresholds` — a 1e-9 change is captured and round-trips.
        This is the bug the phase exists to kill, asserted directly, and mirrors
        the geometry test P1 added.
      * `test_modify_nested_field` — a nested-schema field (`volume.half_extents`)
        is one memcmp/memcpy over the whole nested struct and round-trips like
        any other field. The string flavor only reached it via `init_from_map`'s
        `"volume"` special case.
      * `test_snapshot_is_exact` — values not representable in `write_coord`'s
        5-bit fraction (3.14159265, 0.001) survive a delete/undo exactly. This is
        the regression guard against anyone re-introducing the serialize-based
        clone the plan originally called for.

**Carried into P5 unchanged, as predicted:** the shape (clone capture + binary
field diff) survives the cutover; only the reflection calls get re-pointed from
`Class_Schema` to the generated tables. Undo's text adapter is still deferred to
P5, where map I/O gets rewritten anyway.

---

## Map transfer / switching

ARTIFACT MODEL (decided 2026-07-21). Two artifacts, two audiences — keep them
separate the way Source keeps .vmf and .bsp separate:
- SOURCE (.source text + editor state): mapper-only, edited in the editor. It is
  the INPUT to bake_map. bake_map is an expensive offline step (navmesh today,
  lighting later) so it is NOT re-run on client load. Source NEVER goes over the
  wire to players — a player joining a community server has never seen it and
  shouldn't. serialize_map_to_string / parse_map_from_string are its format.
- COMPILED MAP PACKAGE (bake_map output): the runtime distributable = the .bsp
  analogue. Contains runtime entity data + baked sidecars (navmesh, later
  lightmaps/PVS). THIS is the wire artifact: what the server hosts, what streams
  to a client, what the client caches under maps/ and loads. init_session_from_map
  runs off the deserialized package (map_t + its baked data), not off source.
- WIRE IDENTITY = (map_name, package_hash). The client keys its package cache and
  its "do I need to stream?" check on this, NOT on source. source_hash +
  bake_version are BUILD-SIDE inputs (they decide whether the mapper must re-bake)
  and at most ride inside the package as provenance metadata; they are not the
  wire key. "Reference-first" survives, but the reference is the cached compiled
  package, not the source file.
NOTE: the current step-1 impl hashes the source .source file bytes. That is a
localhost/listen-server shortcut (host == mapper, so source is present). It must
become the package hash before remote clients / streaming are real.

- [x] client honors server's map + FNV-1a hash (CmdAccept.map_path/content_hash);
      mismatch = hard error (no silent desync)
- [x] mid-game map switch: reload_map broadcasts bitstream CmdChangeMap, keeps
      players connected + re-spawns them (spawn_player_for_slot), client reloads
      via load_client_map + Connection_Phase::Loading, acks C2S_MapLoaded;
      server withholds snapshots (client_map_ready) until acked
- byte streaming fallback (S2C_MapData) for clients that lack the COMPILED
  PACKAGE (see ARTIFACT MODEL above — this streams the package, never the source).
  Today the client hard-errors on a missing/mismatched map (load_client_map
  fails, or hash != server); streaming is what lets it recover by pulling the
  package from the server instead. Ordered plan to continue:

  0. [x] DONE (distinct message_id). NOTE the terminology cleanup: the
     fragmentation axis is now message_id / fragment_index / fragment_count (a
     message is split into fragments); the word "sequence" is reserved for the
     FUTURE packet-level ack layer (packet_sequence_number) so the two never get
     confused. convert_to_packets now takes the sender's rolling
     `uint8 &next_message_id` and stamps every fragment of a message with one id,
     advancing the counter per message — so it can't be forgotten (there's no
     un-stamped intermediate state; the old placeholder-0 + separate-stamp API was
     a footgun). Counter lives on Client_/Server_Connection_State; all send sites
     pass it. Receiver already keyed partial_packets[message_id], so a map stream
     and per-tick snapshots no longer share a bucket. Verified by test_udp_socket
     (fragments share an id; consecutive messages get distinct ids). STILL MISSING
     for real networks: UDP has no retransmit — a single dropped fragment loses the
     whole map. Fine on localhost; add ack/retransmit (or resend-until-acked)
     before remote streaming. See "ack/nack system" under Multiplayer Networking
     below.
  1. [x] DONE. Keep the map_t on the server: load_map_into_state loads into
     server_context_t::current_map (retained, cleared on reload) so we can
     serialize without re-reading disk.
  2. [x] DONE. Pure (no-I/O) serialize_map_to_string(map_t) / parse_map_from_
     string(str, map_t) factored out of save_map/load_map; the file fns are now
     thin wrappers. This is the canonical payload — no binary/protobuf entity
     format. (map.cpp/hpp)
  3. [x] DONE. compute_map_content_hash now takes a map_t and hashes the canonical
     serialize_map_to_string() output (not file bytes), so it's
     formatting-independent. Both callers (server_impl load, play_state
     load_client_map) hash the in-memory map_t. Verified by map_migration_test
     (stable across serialize/parse; identical under whitespace reformatting).
     The near-term meaning is the source hash (localhost); the eventual WIRE key
     is the COMPILED PACKAGE hash (entities + baked sidecars) from step 4 — the
     field name stays generic (content_hash) so the meaning can shift without a
     wire change.
  4. [x] DONE. Compiled package format + streaming messages in map_transfer.
     {hpp,cpp}: map_package_t { map_name, entity_text, navmesh } with
     serialize/deserialize_map_package (magic+version container, navmesh floats
     written as exact raw bytes — write_coord's 5-bit fraction would corrupt
     A*), build_map_package(map_t), and compute_map_package_hash (FNV-1a over the
     blob = the eventual WIRE key, unlike content_hash which is entities-only).
     Messages: request_map_data_message_t { map_name } (C2S_RequestMapData) and
     map_data_message_t { map_name, package_hash, compressed, bytes } (S2C_MapData);
     compressed=false for now (step 6 adds gzip; package_hash is over the
     UNCOMPRESSED blob). New Message_Type entries + reassembly branches on both
     sides (ServerInbox::map_data_requests, ClientInbox::map_data_messages) that
     hand raw payloads to the game layer like CmdChangeMap. Verified by
     map_migration_test (package section 7: entity_text/map_name/navmesh
     round-trip, stable hash, corrupted-magic rejected).
     BUG FIXED en route: write_string/read_string (and write/read_c_string) were
     asymmetric — read_byte aligns but write_byte does not, so every string that
     followed a var_uint length (i.e. all of them, incl. CmdChangeMap's map_path/
     map_name) decoded to garbage. Now both use the aligned write_bytes/read_bytes
     block. This was never unit-tested before (map_migration_test only exercised
     the var_uint hash path). Wire encoding of strings changed, but both ends
     share the code and nothing persists it, so it's safe.
     STILL TODO (step 5+): nothing sends C2S_RequestMapData or responds with
     S2C_MapData yet — the reassembly plumbing is in place but the request/respond
     handlers land with the client cache-miss logic in step 5.
  5. [x] DONE. Cache-miss / mismatch now streams instead of hard-erroring.
     CLIENT (play_state.cpp): load_client_map split into finalize_client_map()
     (shared tail: reset replication + init_session + hash + physics + camera,
     operates on this->map) reused by apply_map_package() (parse_map_from_string
     + restore package.navmesh). enter_connected_phase() factored out (sets
     connected/phase + registers console forwarder), shared by the hash-match
     connect and the post-download path. CmdAccept mismatch and CmdChangeMap
     (file-load fail = cache miss, OR hash mismatch) now send C2S_RequestMapData
     via send_request_map_data() and stay in Loading with
     client_context.awaiting_stream_content_hash set (guards resent CmdChangeMap:
     re-request cheaply instead of reloading every tick). S2C_MapData handler:
     verify compute_map_package_hash over the (uncompressed) blob ==
     msg.package_hash, deserialize_map_package, apply_map_package, then
     send_map_loaded_ack with the ENTITIES-ONLY content hash (loaded_map_content_
     hash, set by finalize) so it matches the server's g_state.map_content_hash —
     NOT the package hash. compressed=true is logged+ignored (step 6). SERVER
     (server_impl.cpp): inbox.map_data_requests handler builds+serializes the
     current_map package, streams S2C_MapData (compressed=false, package_hash over
     the blob), and sets client_map_ready[slot]=false so snapshots stay withheld
     until the client acks. Builds clean; map_migration_test + udp_socket_test +
     server_loop_test green.
  5b. [x] DONE (follow-up, same session). Booting with ZERO local map now works.
     on_enter no longer hard-requires a local file: a failed load_client_map is
     non-fatal (logs, session_ready_for_simulation_and_rendering stays false) and we connect anyway. update()
     dropped its top-level `if (!session_ready_for_simulation_and_rendering) return;` — the network poll +
     handshake/CmdChangeMap/S2C_MapData handling now runs mapless; the gate moved
     down to just before Reconciliation so only local sim + rendering-prep are
     skipped without a world. Accept handler streams when `!session_ready_for_simulation_and_rendering` OR
     hash mismatch (was mismatch-only). Server already sets client_map_ready=false
     on the request, so the brief connect→request window of snapshots to a mapless
     client self-heals (entity_updates just fills maps that finalize_client_map
     resets). session_ready_for_simulation_and_rendering is now an explicit orthogonal axis to
     Connection_Phase (world-loaded vs handshake-state); documented on the enum.
     STATE MACHINE: Connection_Phase { Disconnected, Connecting, Loading,
     Connected } is the client's connection FSM — full transition diagram now in
     client_context.hpp. Kept at 4 states (Loading covers both reference-load and
     streaming); session_ready_for_simulation_and_rendering stays a separate flag because on a mismatch switch
     you're Loading WITH the old world still live (session_ready_for_simulation_and_rendering true), vs a
     no-map boot which is Loading with no session.
     Untested at runtime (needs dedicated server + a client missing the map file —
     can't drive the graphical app headlessly); builds clean, unit tests green.
     - [x] DONE. Wire map identifier is now maps-relative, not an absolute path.
  Server sends the basename (current_map_wire_id() = filename of
  current_map_path, e.g. "new_map.source") in both CmdAccept and CmdChangeMap.
  Each client resolves it against its own maps dir via
  shared::resolve_map_path(maps_dir, id) (strips to basename, joins under
  maps_dir). Client maps dir defaults to "maps", overridable by the MAPS_DIR env
  var (read in play_state::client_maps_directory()) — a local dev knob, NOT a
  cvar/CLI-arg (getenv sidesteps the per-DLL cvar-singleton duplication). Used at
  both client resolve sites: on_enter (the last_map.txt entry) and the
  CmdChangeMap handler. Server still LOADS from its own absolute path; only the
  wire id changed.
- TESTING map streaming locally = the "cold client": run MyGame_Client with
  MAPS_DIR pointed at an empty folder so it lacks the map and must stream.
  scripts/run_client_cold.cmd sets MAPS_DIR=cold_maps and launches it (.cmd not
  .ps1 so PowerShell's execution policy doesn't block it). Workflow: start
  MyGame_Server, then run the cold-client script. Normal play is plain
  MyGame_Client.exe (uses ./maps). Still runtime-unverified (needs the GUI).

## P3 — Finish the entity DSL generator  ✅ DONE (2026-07-27)

`entities.def` → `src/tools/entity_gen.cpp` → `src/shared/entities/generated/`.
Full design + rationale in `entity_def.md`. The parser/resolver/codegen, the
factory helpers, the placeable enumeration and the asset manifest scanner all
landed 2026-07-26. What closed the phase on 2026-07-27:

- [x] **Range API settled: a house `Span<T>` (`src/shared/span.hpp`), used
      everywhere.** The generator's five pointer+count signatures
      (`placeable_entity_types`, both `*_manifest`, and the
      `fields`/`field_count` pairs in `entity_type_info_t` /
      `component_type_info_t`) now hand back one value carrying both. The three
      pre-existing `std::span` sites (`input.hpp`, `cvar.hpp` and its callers)
      were converted too, so the codebase has ONE spelling for "a contiguous
      range of T" rather than three.
      * Iterates like any range: `begin()`/`end()` return raw pointers, which
        are already contiguous iterators, so range-for and the `std::ranges`
        algorithms work unchanged.
      * The "generated output depends on nothing but `<cstdint>`" argument for a
        house type was ALREADY spent — the generated header includes
        `linalg.hpp`, which pulls `<algorithm>` and `<cmath>`. The type earns its
        place on consistency, and on not making every consumer of the entity
        tables pay for `<ranges>`.

- [x] **`@runtime_only` placement: KEPT as `X :: entity @runtime_only {`.** It is
      a property of the type, so it sits on the declaration next to the keyword
      it modifies, the same way a field's flags sit next to the field. Kept as a
      negative rather than inverted to `@placeable` because "an entity is a thing
      you put in a level; some are spawned by code instead" is the mental model,
      the majority case (5 of 8) should be the silent one, and it is the safer
      one to forget: a forgotten `@runtime_only` puts a junk type in the
      placement menu where you see it immediately, while a forgotten
      `@placeable` would make a real type quietly missing from it. The attribute
      space it opens up (`@no_network`, `@singleton`, `@category("...")` — the
      first case wanting an argument) is written up in the .def beside it.

- [x] **Asset naming settled, with no qualifiers left.** `Unit_Sphere` /
      `Unit_Pyramid` are gone, and so is the `Missing`/`Error` duplicate:
      * `resources/obj/sphere.obj` (3.5 MB) and `cube.obj` were referenced by
        NOTHING. Deleted — which frees `Sphere` for the generated sphere and
        leaves `Box` without a `Cube` synonym.
      * `generate_pyramid_mesh` was registered and never once requested
        (`__primitive_pyramid` appears at no call site). Deleted, so `Pyramid`
        is pyramid.obj — the one the editor actually draws.
      * The scan now SKIPS the placeholder's own file, so error.obj has one id
        (`Missing`) instead of two under two names. That collision was created
        by the generator itself, so its duplicate-name check could never fire on
        it; `entity_layout_test` guards it instead.
      * Manifest is now: Missing, Isosphere, Pyramid, Box, Arrow, Sphere,
        Cylinder, Cone, Wedge. Zero collisions, zero invented names.
      * **`procedural` SURVIVES, and the reason is a real finding.** The clean
        end state bakes the generators to .obj so "a mesh asset is a .obj in
        resources/obj" is the whole rule and the source column disappears. That
        is blocked on `load_obj` NORMALIZING every .obj to a 100-unit max extent
        while `get_primitive_mesh` returns UNIT meshes that callers scale
        themselves — the two regimes differ by ~100×, so a baked primitive loads
        at the wrong size. Recorded at both sites in `asset.cpp`/`asset.hpp`;
        the migration is in todo.md.

- [x] `entity_layout_test` is at 40 checks and covers all of the above.

## P4 — The flag audit  ✅ DONE (2026-07-27)

`@Networked`/`@Saveable` were decorative in the macro system (only `@Editable`
was ever read, by `entity_inspector.cpp`) and are now real. Every field in
`entities.def` was decided rather than transcribed, with the reasoning written
at each declaration in the .def itself — this is the summary.

The rule that did most of the work: **a map-placed entity's static config does
not need `@Networked`, because the client loaded the same map.** Replicating it
spends bandwidth telling the client what it cannot fail to already have.

- [x] **`Light_Entity`: dropped `@Networked` from all seven fields.** Lights are
      map-placed and read back out of the map. (Also worth knowing: nothing in
      the renderer reads a `Light_Entity` at all yet — its only consumers are
      the editor traits and picking bounds.) Dynamic lights are runtime-spawned
      and would earn it back.
- [x] **`Box_Volume`: dropped `@Networked`.** Its only user is
      `Trigger_Volume_Entity`, which is invisible and entirely server-side. It
      was putting a position and half-extents on the wire for every trigger in
      the level, for no reader.
- [x] **`Rocket_Entity`: dropped `@Networked` from `lifetime`, `damage_radius`,
      `damage_amount`, `knockback_force`, `owner_id`.** `rocket_system.cpp` is
      the only reader of all five; the client draws a rocket from `render` /
      `position` / `orientation` / `hitbox` and reads nothing else off it.
      `velocity` stays — it is what snapshot interpolation will need.
- [x] **`Physics_Body_Entity`: `shape` and `size` are editor/server config, not
      wire data.** The visual size rides in `render.scale`, which the spawn path
      sets from the same extents. `velocity` stays networked for interpolation.
- [x] **`Render` KEEPS `@Networked`, and it is the case that earns it**: rockets
      and physics bodies are spawned at runtime, so a client that never had them
      in its map learns what to draw only from the wire.
- [x] **Every `@Editable` on a `@runtime_only` type dropped** (Player, Weapon,
      Rocket). The inspector only ever walks map entities, so they were
      unreachable.
- [x] **Particle emitter: `emitter_lifetime` / `parent_entity_id` were
      `@Networked @Editable` and are read by NOTHING** — grep finds no use
      outside the schema registration. Emitters are built from the client's own
      map load (`play_state.cpp:1399`), so nothing on that entity is networked.
      `emitter_lifetime` is now authorable and persisted; `parent_entity_id` has
      no flags at all, since a uid in a map file means nothing.

Two contradictions became BUILD ERRORS rather than silently-ignored flags:

- [x] **Flags on a component-typed field.** The component's own field flags are
      what every consumer reads, so a flag at the use site is read by nobody.
      That is exactly what `volume: Box_Volume @Editable` was doing, sitting
      next to four unflagged component fields meaning the same thing.
- [x] **`@Editable`/`@Saveable` on a `@runtime_only` entity.** Neither can ever
      be read for such a type. The base's fields are exempt by construction:
      `Entity` is not itself `@runtime_only`, and its flags describe the
      map-placed types that inherit them.

Both report file:line:column and name the fix; verified firing against a scratch
.def. `entity_layout_test` separately checks the emitted tables agree, so the
decisions cannot be reverted silently.

- [x] **Orientation units DECIDED and documented** (in `entities.def`):
      canonical is **Euler XYZ in DEGREES**. That is a reading of the code, not
      a preference — `renderer.cpp:2864` multiplies `draw_mesh`'s rotation by
      `DEG2RAD` and `physics_body_system.cpp:122` writes `quat_to_euler_degrees`
      back, so both halves already agreed and the ambiguity was in the
      documentation. Quaternions deferred until snapshot interpolation makes
      slerp worth the migration. Also recorded: a Player's heading is
      `view_angle_yaw`/`view_angle_pitch` and its motion is `velocity`;
      `orientation` is written once at spawn (`server_impl.cpp:455`) and is
      vestigial thereafter — which is why there is no orientation/velocity
      relation to enforce.

## Physics body / Jolt (done: basic wiring)
- [x] physics_body_entity (schema + entity_list registration)
- [x] physics_body_system: `spawn_physics_body` (box/sphere) +
      `update_physics_bodies` (reads Jolt transforms back)
- [x] `spawn_cube` / `spawn_sphere` console commands (server-flagged)
- [x] `step_physics()` + `update_physics_bodies()` wired into `server::Tick()`
- [x] integrated-mode render path via `server_session` on `client_context_t`
- [x] networked replication: slot=254 sentinel in serialize/deserialize, delta
      compression matches the rocket pattern

## Multiplayer Networking (done: basic wiring)
- [x] fixed server tick loop (accumulator-based, sv_tickrate)
- [x] client connection handshake (CmdConnect/CmdAccept/CmdReject)
- [x] client sends C2S_PlayerMoveCommand to server each tick
- [x] server runs player_move() authoritatively on received input
- [x] server broadcasts S2C_EntityPackage snapshots to all clients
- [x] client receives and deserializes entity snapshots
- [x] client-side prediction with server reconciliation
- [x] remote player interpolation (2-snapshot buffer)
- player movement is dependent on delta t. that does not seem good.. is the fps too high? should we limit fps?