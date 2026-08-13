# Completed tracks

Phase order and rationale live in `todo.md`; design rationale in
`entity_def.md` (entities), `entity_storage_def.md` (P7 storage/ownership),
`entity_system_def.md` (pool retirement) and `cvar_def.md` (cvars). This file
keeps the finished work so `todo.md` stays a work list.

- **THE ENTITY TRACK, P0–P7 plus pool retirement** — the generator, schema,
  storage and wire chain. Everything except **P8 (protobuf removal)**, which is
  the one entity-track item still open and still in `todo.md`.
- **CVAR TRACK (def_gen), steps 1–6** — cvars and console commands onto the
  same DSL + generator. Jump to it below; the "forward loop" post-mortem at
  its end is the part most likely to matter again.
- **MODULE OWNERSHIP TRACK** — the rest of the static-lib duplication after the
  cvars: the asset registry, `debug_collision::g_collision_faces`,
  `bot_debug::g_entries`.
- **PLAYER DIMENSIONS + RENDERER PASS (2026-08-04)** — the player's three
  conflicting sizes collapsed onto one hull, the editor/runtime spawn-origin
  mismatch, and two renderer API fixes.
- **ANIMATION TRACK, build-order steps 1–2 and 7 (2026-08-08 → 2026-08-09)** —
  the Blender exporter, the `.skeleton`/`.mesh` loader, textures, GPU skinning in
  bind pose, and the authored aim pose set driven by view angles. Remaining
  animation work is in `animation_def.md` under "WHAT'S LEFT". The Blender
  gotchas at the end of that section are the part most likely to matter again —
  especially that a rig's pose is never evaluated headless in Object mode.
- **THE ANIMATION TOOL, PHASES A + B (2026-08-10)** — the pose preview, and the
  `rig.hitboxes` bone-span volumes drawn under it with the derived-radius seed
  and the two audit readouts. The bone-axis correction in it (a bone points down
  minus its THIRD column) is the part most likely to matter again.
- **SKELETAL HITBOXES IN GAMEPLAY (2026-08-11)** — animation step 4: hitscan
  against the posed volumes instead of three static boxes, `body_yaw` pulled
  forward into server ownership to make it possible, and one placement function
  shared by the server's hit test and the client's overlay. Lag compensation is
  what is still missing.
- **HIT FEEDBACK + AIM DEBUG RIG (2026-08-09)** — the bot yaw convention fix,
  the aim-pose debug rig (the blend turned out to be unreachable from any
  gameplay state), `unlit_textured_skinned`, `-Werror=switch`, and hit sounds
  split across the two event channels. The cosmetic-effect vs replicated-state
  rule at the end of it is the part most likely to matter again.
- **THE HOUSE FIXED-SIZE ARRAYS (2026-08-11)** — `Array<T, N>`,
  `Enum_Array<Enum_T, T>` and `rows_in_enum_order` in `shared/array.hpp`, closing
  `todo.md`'s items A and B in one go. Jump to it below; the thing most likely to
  matter again is that `Enum_Array` does NOT catch a short initializer.
- **Closed decisions** — the appendix at the bottom, recorded so they are not
  re-litigated.

# THE ENTITY TRACK — completed phases

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
     sides (ServerInbox::map_data_requests, Client_Inbox::map_data_messages) that
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

## P5 — Hard cutover (delete the macro system)  ✅ DONE
*branch `p5-hard-cutover` · the tree builds and **all 17 test executables pass**.*

The macro system is deleted, every consumer is converted, and the tree builds.
The phase's one hard rule — do not stop while the tree is broken — is satisfied.

- [x] `Class_Schema`/`Field_Prop` deleted (`network/schema.{hpp,cpp}` gone)
- [x] X-macro / factory registration deleted (`entity_list.hpp`,
      `entity_type.hpp`, all 7 `*_entity.{hpp,cpp}`, `entity.{hpp,cpp}`)
- [x] Dynamic dispatch converted. `network::Entity` and all its virtuals are
      gone; `entity_as` compares the generated `T::static_type`, `get_schema`
      is the generated tables, `get_box_volume` is a component-table lookup.
      The only per-type virtual that actually existed was
      `Trigger_Volume_Entity::get_box_volume` — see the lifecycle note below.
- [x] Map serialization on the generated tables, honoring `@Saveable`, in
      **declaration order**. One key per leaf, dotted for components
      (`volume.half_extents`) — the old `key:value|key:value` blob could not
      even represent a component inside a component.
- [x] Versioning without version numbers, all three cases exercised by the real
      maps: missing key → DSL default; unknown key → **warning** and ignored
      (every pre-cutover map carries `entity_id`, which is `@Networked`-only
      now); renames via one-time conversion in `map.cpp`.
- [x] Undo re-pointed at the generated tables (`entities::capture_field_changes`
      / `write_field_changes`), transaction snapshots via `clone_entity`.
- [x] `__primitive_` retired in full. `assets::init()` walks the manifests
      eagerly and is called from all three launchers; `render.mesh` is a
      `mesh_asset` id; all 7 `strncmp` dispatch sites and `get_primitive_mesh`
      are gone.
- [x] **Geometry keeps free-form mesh paths** — decided, not deferred. A static
      mesh is arbitrary level art, so the closed set an asset id gives you is
      the wrong shape: an author adding a prop should not have to touch
      `entities.def`. Recorded at `geometry_surface_t::mesh_path`.
- [x] Generator grew what map I/O and the inspector actually needed: an
      `enum_type` table + `enum_id` column (a field record could not previously
      say WHICH enum it was, so no generic walker could read one),
      `asset_class_manifest(id)`, and `static_type` on every entity struct.
- [x] `network_test`'s SEGFAULT is **fixed, and it was the test's bug**: it
      called `diff(nullptr, &entity, schema)`, and `diff()` memcmp'd the
      baseline unconditionally. `Entity::serialize` had its own null-baseline
      branch, so only the test ever hit it. It now drives the real
      serialize/deserialize path — which is also the only option left, since a
      test cannot invent an entity type against a closed enum.
- [x] Trigger actions: the string-keyed, static-init `Trigger_Action_Registry`
      and its X-macro name list are gone, replaced by one exhaustive switch on
      `entities::Trigger_Action` (`server/trigger_actions.hpp`). The property
      the registry existed for survives — map files still store the enum by
      NAME, so reordering rebinds nothing.
- [x] **Schema hash in the connect handshake.** `CmdConnect.schema_hash` carries
      `entities::SCHEMA_HASH`; the server refuses a mismatch before a slot is
      taken, `log_error`s both hashes, and echoes the server's in
      `CmdReject.server_schema_hash` so the client can report both too.
      Verified against a live `MyGame_Server` with a raw UDP prober: matching
      hash → CmdAccept, `0xdeadbeef` → CmdReject naming both hashes.
- [x] **`asset_test` now passes.** Two bugs, both in the test: the fixtures were
      hardcoded to POSIX `/tmp` (now `std::filesystem::temp_directory_path()`,
      and the ofstream state is checked instead of failing silently), and it
      asserted `channels == 3` when `load_texture` deliberately forces RGBA.
      **All 17 test executables are green.**
- [x] `CLAUDE.md` rewritten: "Schema System" → the DSL + generator, a new
      "Entity reflection" section, "Asset System" covers the manifest, the
      handshake is documented, and the stale port numbers (2020/2024 → the real
      9999/5001) and test list are fixed. `src/shared/entities/README.md` was
      worse — it still said the macros were what the game builds against — and
      is rewritten too.
- [x] `maps/new_map.source` and `maps/other.source` converted (backups at
      `*.preconvert.1.bak`). `map_convert` decided "needs conversion" by looking
      for retired geometry classnames, which is geometry-only and so said
      "already converted" for maps whose ENTITY text was still pre-cutover; it
      now compares the file against what `save_map` would write (line endings
      normalized), so it stays correct as further conversions land. Its backup
      no longer overwrites an existing `.preconvert.bak` — that held the only
      copy of the pre-geometry-exit original.
- [x] `placement_tool.cpp`'s static-mesh prototype pointed at the nonexistent
      `resources/obj/m4a1_s.obj`; now `error.obj`, the question-mark
      placeholder. Geometry mesh paths are free-form, so nothing would have
      caught it at compile time — it just placed an invisible object.

**Notes for whoever reads this later:**

- **Lifecycle hooks were NOT built, on purpose.** The item assumed per-type
  virtual overrides to replace; grepping found exactly one
  (`Trigger_Volume_Entity::get_box_volume`), and it became a component lookup.
  There is no `on_entity_spawned` caller to write against, so building the hook
  now would be inventing an interface with no user. The sanctioned pattern —
  a handwritten exhaustive switch over the closed enum — is used where it does
  earn its place: `make_entity_pool`, `create_map_entity`, `fire_trigger_action`,
  `compute_entity_bounds`, and the editor's `ENTITY_DISPATCH`.
  *(`make_entity_pool` is gone as of 2026-07-30 — pool retirement step 3. It was
  the one on that list dispatching STORAGE rather than behaviour, which the
  generated table answers as data. The other four stand.)*
- **Undo's text adapter** landed as `entities::field_to_text` /
  `field_from_text` rather than as a `field_change_t`-shaped pair. Same seam,
  and map I/O is its real (and only) caller — a second encoding for undo alone
  would have had no user either.
- **Physics cubes are now hittable.** The `Shape_Kind` merge fixed a live bug:
  `physics_body_system.cpp:62-64` set `body->shape_type` AND
  `body->hitbox.shape_type` from the same string, so `spawn_cube` wrote `"box"`
  into a hitbox — and `test_hitbox_collision` (`components.cpp:109-194`) only
  branched on `"sphere"`/`"capsule"`/`"aabb"`, so a cube's hitbox fell through
  every strcmp to `return false` and never registered a hit. Correct now, but a
  behavior CHANGE.
- **Weapons are out of the editor placement menu**, as decided in
  `entities.def`. The old menu offered EVERY type from the X-macro (rockets and
  players included) and `placement_tool.cpp:231` special-cased placing a
  `Weapon_Entity`. `placeable_entity_types()` replaces that menu and
  `Weapon_Entity` is `@runtime_only`, so it goes: a gun on the ground should be
  a pickup entity that does not exist yet, and no system spawns or ticks a
  `Weapon_Entity` at all.
- `spawn_physics_body` now takes `Shape_Kind`, not `const char*`.
- **`Light_Entity` is read by nothing in the renderer.** Only the editor traits
  and picking bounds touch it. Its fields lost `@Networked` in P4 on the
  map-placed argument; when lights actually light something AND are spawned at
  runtime, that reverses.

---

## P6 — Serializer v2 (with snapshot delta compression)  ✅ DONE
*Full build green; all 18 test executables pass. The two live-GUI checks that
could not be done headlessly were verified 2026-07-29 — see "Verified live"
below. Nothing of P6 remains in `todo.md`; `@interpolate` outlived the phase and
is now its own entity-track item.*

One narrow seam: **"give me the changed fields."** Change *detection* stays a
separate module from wire *encoding*.

- [x] ~~Recursive generated visitors over the schema tree replace the flat
      `Field_Prop` walk~~ — landed with P5. `networked_leaf_fields(type)` is the
      flattened, cached walk and both ends build the bit order from it, so bit N
      means the same leaf on both sides by construction.
- [x] Detection side stays memcmp-against-baseline (protected by blittability);
      dirty-bit / change-tick tracking swaps in later, at `serialize_entity`'s
      two memcmp passes and nowhere else.
- [x] **Encoding side got snapshot delta compression, done properly: the
      baseline is the ACKED snapshot, not the last-sent one.** See below.
- [x] Change-notification seam: `deserialize_entity` takes an optional
      `network::changed_fields_t*` out-param, indexed like
      `networked_leaf_fields(type)`. It is the mask the reader had to buffer
      anyway, so it costs nothing. No caller yet — the first one is
      "mesh id changed → reload asset".
- [x] ~~Fix `network_test` first~~ — was already fixed in P5.

### What acked baselines actually changed

The delta path existed before this and was **wrong on an unreliable channel**:
the server deltaed against the snapshot it last SENT and the client applied the
delta to its CURRENT state. One dropped datagram and every field that then
stopped changing was permanently wrong on the client — it never learns the value
it missed, because the server believes it already sent it. Reproduced as a test
(`snapshot_delta_test`, third subtest) so the reason survives.

Now:

- `C2S_PlayerMoveCommand.acked_server_tick` — the client names the newest
  snapshot it fully reconstructed. Rides on the move command: same rate, same
  destination, no extra datagram.
- `S2C_EntityPackage.delta_from_tick` — the server names what the payload is a
  delta against. 0 = full update.
- `shared/network/snapshot_history.hpp` — one ring type used by BOTH ends (32
  ticks ≈ 530ms at 60Hz). The server stores what it sent so it can delta against
  the acked tick; the client stores what it reconstructed, because the live
  world has moved on by the time the server names that tick.
- A client that no longer holds `delta_from_tick` drops the packet WHOLE and
  logs it. Its ack does not advance, so the server re-baselines to a full update
  within a round trip. Self-healing, no reliability layer needed.
- Server tick numbering now starts at **1**: 0 is the wire sentinel for
  "no baseline".
- `net_snapshot_debug` (client cvar, default off) prints `delta_from` and the
  payload size every 120 ticks. `delta_from 0` every line = the ack loop is
  broken and everything is a full update.

### Explicit entity removal, and what it unlocked

The delta path was field-accurate but **membership-blind**: every snapshot
listed every entity, because "absent means gone" was the only way to express a
despawn. So an entity that had not moved still paid its key and an all-zero
change mask, every tick, forever — these were whole-world snapshots wearing a
delta's clothes.

**The canonical fix is a removal record INSIDE the snapshot delta, not a
separate spawn/despawn channel** — Quake 3's `SV_EmitPacketEntities` and
Source's `EnterPVS/DeltaEnt/PreserveEnt/LeavePVS` are the same idea. Membership
is a property of "the world at tick N", exactly like a field value, so it is
deltaed against the acked baseline exactly like one — and it inherits that
rule's reliability for free. Lose the datagram that says "entity 47 is gone" and
the client's ack does not advance past it, so the next snapshot is computed
against an older baseline that STILL CONTAINS 47 and says it again. Self-heals
within a round trip, no retransmit layer, for the same reason a changed field
does. `snapshot_delta_test` reproduces exactly that (subtest: "a dropped removal
re-rides on the next snapshot").

A separate despawn channel was the plan recorded in `todo.md` and it is the
worse shape: it needs its own retransmit AND its own ordering against the
snapshot stream. A despawn landing before a snapshot that still lists the
entity, or after one that already dropped it, is a second source of truth about
world membership disagreeing with the first.

**The payoff is not the deletes — it is that ABSENCE NOW MEANS UNCHANGED.** The
receiver seeds the frame from the baseline and applies records on top, so only
spawns, changes and removals ride the wire. That is the actual step from
whole-world snapshots to per-entity deltas. Measured in the test: 3 rockets, one
moving = 63 bytes / 3 records before, 6 bytes / 1 record after; a tick where
nothing moves is one var_uint.

- [x] **`shared/network/entity_snapshot.{hpp,cpp}`** — the whole-snapshot codec,
      the set-level counterpart to `entity_serialization`'s field-level one.
      Grammar in the header:
      `snapshot := record_count:var_uint record*` /
      `record := type:var_uint uid:var_uint removed:bit payload?`
- [x] **Spawn needs no opcode.** An entity with no baseline entry is written
      with every mask bit set — that IS a full update — and the receiver starts
      it from a default-constructed value. Only removal needed a bit.
- [x] **The 255/254 "special slot" sentinels are gone**, replaced by
      `entities::entity_type` on the wire. They shared a number space with
      client slots, so they would have collided once bot slots reached 254
      (`BOT_SLOT_BASE` is 32). Enum values shift when `entities.def` changes,
      which is exactly what `SCHEMA_HASH` refuses a build over, so sending the
      raw enum is safe. Adding a replicated type is now a map on
      `snapshot_frame_t` plus a case in an exhaustive switch, not a hunt for an
      unused magic number.
- [x] **An undecodable record fails the whole packet, loudly.** Payload length
      is only knowable from the type's field table, so an unknown type cannot be
      skipped — `deserialize_snapshot` returns false, the client drops the
      packet whole (including the effect batch trailing it), and the unadvanced
      ack makes the server re-baseline.
- [x] **ONE frame type shared by both ends** (`network::snapshot_frame_t`,
      keyed by entity uid). The server deltas against what it believes the
      client reconstructed, so the two being the same type is the guarantee, not
      a convenience. The client's by-slot `last_player_entities` is now a VIEW
      rebuilt from the frame on publish.
- [x] **Baseline lookup is O(1)** (was a linear scan over the frame's vectors,
      per entity per client per tick). Fell out for free: the frames had to be
      keyed by uid for the removal diff anyway. *(Was its own open P6 item.)*
- [x] **One shared frame ring plus per-client ack cursors** (was
      `CAPACITY × sv_max_player_count` full copies of the world = 1024 frames to
      hold 32 distinct ones). The frames are identical for every client because
      there is no PVS; what is genuinely per client is a uint32.
      `Snapshot_History::acked_tick`/`acknowledge`/`baseline` are documented as
      the single-peer convenience — the client uses them, a multi-peer sender
      keeps its own cursors and calls `find()`. *(Was its own open P6 item.)*
      Cleared on map switch and on slot reuse, so neither a new world nor a new
      occupant inherits a stale baseline.
- [x] **Everything client-side is derived from the RECONSTRUCTED FRAME, not
      from the records that built it.** Load-bearing now that an unchanged
      entity produces no record: reconciliation and remote-player interpolation
      used to be driven from inside the record loop, so they would have stopped
      happening the moment a player stood still.

**Live bug this fixed:** a disconnected player's green wireframe box rendered
forever. `Remote_Player_State::active` was set true and never once set false —
and before explicit removal it *couldn't* be cleared correctly, since "not in
this packet" didn't mean "gone". `remote_players` entries are now pruned against
the frame's membership. `Remote_Player_State` also gained `entity_uid` so a slot
that changes occupant restarts its interpolation buffer instead of lerping the
new player in from the old one's last position.

**Deleted:** `S2C_EntityPackage.expected_max_entities` and `update_baseline`
(fields 1 and 3, now `reserved`). The first existed *because* removal was
implicit; a record count is not an entity count, and the one at the head of
`entity_data` is the only count that means anything now.

### Verified live (2026-07-29) — the two checks that needed a running client

- [x] ~~Runtime-verify the ack loop live~~ — a moving player logged
      `delta_from` at the previous tick for ~20 bytes, dropping to ~3 bytes
      standing still. Constant while idle is the point: absence-means-unchanged
      works, and the 3-vs-~1 byte floor is frame framing, not a leak.
      *(The line came from `net_snapshot_debug`. Its format has since grown the
      rocket and body counts — `play_state.cpp:676` is the current text, so the
      original quote no longer reproduces verbatim.)*
- [x] ~~Disconnect test~~ — two local clients, a disconnect leaves no lingering
      wireframe. Unblocked by the ephemeral-port fix: clients bind `open(0)`
      instead of a fixed 5001, so the server can tell two of them apart.

---

## P7 — Storage refactor: one ownership model  ✅ DONE (2026-07-30)
*Full build green; all 19 test executables pass. **Design AND full log:
`entity_storage_def.md`** — it has the audit, the handle decision, and §6's
step-by-step record of what each step did and what it turned up. This is the
summary; that file is the source of truth.*

One ownership model for runtime entities: the uid is the handle, the pools are
dense, and map load stops writing into the map.

**Decided (§2): the handle is the bare `entity_uid_t`, not a generational pair.**
A monotonic uid is never reused, so a stale uid resolves to *nothing* rather than
to a different entity — which is the whole guarantee a generation counter buys.
And the uid is already the identity at three boundaries that cannot carry
`{index, generation}`: the map file, the wire (`owner_id: u32 @Networked`,
`snapshot_frame_t`'s keys) and Jolt's `entity_body_map`. A generational handle
would have been a *second* identity for the same thing. It stays available as a
pool-internal optimisation later, because the public name is the uid either way.

**Decided (§3): pools stay DENSE** (swap-and-pop, no tombstones), so a per-type
snapshot stays a straight copy of the vector. Stability comes from the uid index,
not from stable slots. **Decided (§5): `game_session_t::geometry` does not join
the pool model** — already a session-owned value vector since P1, never spawned,
destroyed or networked.

- [x] **1. Map load stops mutating the map.** `Entity_Pool_Base::add_existing`
      takes the uid and stamps it on the pool's **copy**; the
      `entry.entity->entity_id = entry.uid` line in `init_session_from_map` and
      its twin in `add_entity` are gone. `const map_t&` is true.
      * The test that matters reads the map entity's `entity_id` **directly**
        (`session_test`), *not* the content hash — `entity_id` is not
        `@Saveable`, so the hash comparison §4 originally proposed would have
        passed against the very bug. Also covered: two sessions from one map,
        which is the original failure.
- [x] **2. The uid index.** `Entity_System::locations` (uid →
      `{entity_type, slot}`) plus `get<T>(uid)`, which answers nullptr for both
      "no such uid" and "uid names a different type" — that is what lets a caller
      use it as lookup *and* type test in one `if`. Guarded by
      `validate_locations()`, which cross-checks index against pools in both
      directions; verified by deleting the swap-and-pop fixup and confirming
      `session_test` fails naming the stale uid.
- [x] **3. Delete the uid linear scans.** Five copies of `find_player_by_uid` /
      `find_physics_body_by_uid` gone, plus `bot_system`'s per-bot-per-tick scan
      of the whole player pool (`Bot_State` now carries `entity_uid`).
- [x] **4. The compile-breaking one:** `spawn<T>()` returns a `entity_uid_t`,
      `destroy` takes one, `destroy(T*)` and its pointer-range check are gone.
      Establishes the rule the whole phase exists for: **never store a `T*`
      across a call that can spawn or destroy in the same pool** — checkable by
      reading one function, where the old rule was "never store one at all, and
      also hope".
      * The slot scans went too, and needed a different answer: "which player is
        slot N" is a *slot* question the uid index cannot serve, so
        `Player_Server_State` gained a `player_uid` column.
- [x] **5. `unregister_physics_body` wired into destruction.**
      `server::destroy_entity(context, uid)`
      (`src/server/entity_lifecycle.{hpp,cpp}`) is the one server-side
      destruction funnel — **a function, not a hook installed on the session.**
      A `std::function` the server installs so `Entity_System::destroy` could
      call back into physics is registration wearing a different hat: invisible
      at the call site, silently absent on any `Entity_System` nobody installed
      it on, and the exact shape P0–P6 and the CVAR TRACK spent their time
      deleting. The residual is stated rather than papered over: it is a
      convention, not a compiler-enforced funnel, accepted because only the
      server destroys anything.
      * **The leak this step was written to fix never existed.** Rockets own no
        Jolt body (moved by hand, hit-tested with `cast_sphere`), and a rocket
        kill does not destroy the victim — `schedule_respawn` reuses the entity.
        The step made a future leak unrepresentable; it did not fix a live one.
      * **The audit found a real bug instead.** `load_map_into_state` cleared
        `Player_Server_State::player_uid` because a new session restarts
        `next_entity_id` and a retained uid can be *reissued* — but never cleared
        `death_tick_by_player_uid`, keyed the same way. A death pending across a
        map switch would resolve to a real but unrelated `Player_Entity` and
        reset its position and health when the delay elapsed.

**Step 6 (the runtime cycle) was CLOSED BY DECISION, not by verification —
2026-07-30, and the distinction matters if something turns up later.** The step
asked for a hand-driven map load → play → save in the integrated build (connect,
fire rockets, take a rocket kill, leave, `spawn_cube`, a `map` switch with a
death pending). That was **not run.** The call was that steps 4–5 fail loudly and
in-your-face if they are wrong, so ordinary play is a sufficient detector and a
scripted pass buys little. All 19 tests are green, which is the half that *was*
verified. If a P7-shaped symptom appears — an entity resolving to the wrong one,
a stale uid, a body that outlives its entity — this is the unswept corner.

*(One thing found while checking: `spawn_cube` draws nothing. Diagnosed and NOT
a P7 regression — it is the duplicated asset registry, tracked in `todo.md`.)*

**Left open deliberately when the phase closed:** whether `map_entity_t` should
hold a value rather than a `shared_ptr`. Real question, but an *editor* refactor
— the tools assume stable pointers into a live `map_t` — so it moved to the
Editor list in `todo.md` rather than being bundled here, per P7's own ordering
rule.

**Two P7 bullets the audit struck off as stale** (§1): *unify the runtime entity
id type* (already u32 everywhere; the uint64 died with the macro system in P5)
and *factory stops returning `shared_ptr`* (those symbols no longer exist;
`create_map_entity` **keeps** returning one, and §4 says why).

---

## Pool retirement — delete the pre-generator `Entity_System`  ✅ DONE (2026-07-30)
*Full build green; all 19 test executables pass. **Design AND full log:
`entity_system_def.md`** — §5 records what each step did and what it turned up.
This is the summary; that file is the source of truth.*

Deliberately **not numbered**: `P8` already means protobuf removal in this file
and in `entity_def.md`, and renumbering to buy a tidier sequence would have
stranded those references. It sat between P7 and P8 in the track.

`Entity_System` predated the DSL. Its whole structure — `Entity_Pool_Base` and
its five virtuals, heap pools behind `unique_ptr`, a `std::map` keyed by tag, and
`make_entity_pool`'s hand-written switch naming all eight types — existed to
answer *"runtime tag to compile-time `T`"*. `entity_info(type)` already answered
it as data (`size_in_bytes`, `alignment`, `construct_at`), and `construct_at`'s
own doc comment named pooled storage as its intended client. The pool was the one
caller on that list that never took the hook.

**Decided (§1): the pool becomes a plain struct and the entity type becomes a
field.** `Entity_Pool<T>` used `T` for four things — `sizeof`, default
construction, copy-assign, `entity_as<T>`'s tag compare — and the first three are
table columns while the fourth is a compare against `Entity::type`. Nothing was
left that needed a template parameter, so nothing needed the switch that existed
to supply one.

**Storage stays dense.** `entity_storage_def.md` §3 had settled dense vs.
slot-stable and this reopened nothing. `Stable_Array` (`shared/array.hpp`) was
re-examined and **declined**: it is itself a template on `Type`, so it would have
left the switch exactly where it was, and its compile-time `Slot` sizing plus
address-masking make it the harder of the two to type-erase. It also has zero
users and zero tests — if it ever ships, not by debuting in the hottest structure
in the game.

- [x] **1. The invariants, proven before anything rested on them.** `def_gen`
      emits per entity `static_assert` for `is_trivially_copyable_v` /
      `is_trivially_destructible_v` / `is_base_of_v<Entity, X>`, plus one new
      table column `Entity* (*as_base)(void* memory)`. Additive and
      behaviour-free — nothing called the column yet — so a green build *was*
      the proof that the rest of the phase was legal at all. 24 asserts (8 × 3),
      green.
      * The four **hand-written** stand-in asserts (`entity_layout_test.cpp`,
        covering four of the eight types) are **deleted**. That hand-maintained
        per-type list is the exact shape this phase existed to remove; including
        the generated header is the check now.
      * Load-bearing afterwards, not decoration: step 3 makes `push_copy` and
        `remove_at` `memcpy` and the byte pool runs no destructor, so the day a
        non-trivial field lands the pool leaks and nothing else would say so.
        **Verified by breaking it**: a `std::string` injected into
        `Player_Entity` fails the copyable *and* the destructible assert, each
        naming the type and the reason. An assert nobody has seen fire is a
        comment.
      * `as_base` exists so the pool reaches `Entity*` from `std::byte*`
        **without betting on `Entity` at offset 0** — `Entity` has data members
        and so does every derived type, so they are not pointer-interconvertible
        and there is no honest `static_assert` for that layout. The generator
        emits the `static_cast` where the type is complete instead. Since no
        assert can cover it, `entity_layout_test` covers it at **runtime**:
        `as_base` must return the same pointer the language does, and every type
        must carry both hooks (the pool indexes by tag and cannot see a hole).
      * **`SCHEMA_HASH` did not change**, so this build still connects to one
        without it. Expected — the hash is mixed from the parsed `.def`, not the
        emitted text — but confirmed rather than assumed, since a table-shape
        change *looks* exactly like what the handshake guards.
      * Ordering footnote: written to land ahead of P7 step 4, because it is a
        *reflection* change and the ordering rule forbids those riding along with
        storage steps — leaving "before P7 step 4 or after P7" as the only legal
        slots. P7 step 4 landed first, so it took the second slot. The proof
        still arrived before the storage swap, which is the only place it was
        load-bearing.
- [x] **2. Narrow the accessor, keep the storage.** `get_entities<T>()` →
      `Span<T> entities_of<T>()`, still backed by `std::vector<T>`. 15 call sites
      (was 17 — P7 step 4 deleted two `get_entities<Player_Entity>()` scans), no
      storage change. This is the seam that made step 3 a one-file change, and it
      narrows callers, who could previously `push_back` into a pool behind
      `locations`' back through the `std::vector<T>*` they were handed.
      * **Every `if (!pool)` null check went with it** — 9 of them. An empty span
        answers "no entities of this type" and "this type has no pool" the same
        way, which is correct: the second was a registration bug, never a
        condition worth branching on, and step 3 made it unrepresentable.
      * **The one place it was NOT mechanical**, and the reason to have done this
        as its own step: `server_impl.cpp`'s per-tick trigger walk grabbed
        `player_pool` and the snapshot frame build ~100 lines later reused it.
        That was safe with a `std::vector<T>*` — the pointer survives a
        reallocation and only its ELEMENTS move — and is not safe with a span,
        which carries the data pointer *and* the count. It re-fetches now, next to
        the rocket and physics-body fetches it already did. Latent rather than
        live (nothing between them spawns a player: `fire_trigger_action` →
        `inflict_damage` neither spawns nor destroys), but it is the difference
        between a rule callers are told and a rule the type enforces.
      * Two long-range holds were audited and kept: `bot_system`'s `players` (the
        only spawn under it is a Rocket, a different pool) and `server_impl`'s
        bot-spawn loop over the spawn-marker pool (`spawn_bot` spawns a
        `Player_Entity`, also a different pool). Both now say so at the site.
- [x] **3. The storage swap.** `Entity_Pool` is a plain struct over
      `std::vector<std::byte>` (`type`, `stride`, `count`), pools are
      `std::array<Entity_Pool, ENTITY_TYPE_COUNT>`, and everything in §3 of the
      design is deleted — `Entity_Pool_Base` and its five virtuals,
      `Entity_Pool<T>`, `make_entity_pool`, the `std::map` of `unique_ptr`s,
      `register_all_known_entity_types`, the `@NOTE(SJM)` constructor apology,
      `invalid_entity_slot` and `uid_at`. It came in at the predicted size:
      `entity_system.{hpp,cpp}` plus three one-line outside edits.
      * `play_state.cpp`'s debug overlay kept working as predicted — `pool.type`
        is a member rather than a map key, and index 0 (Invalid) is skipped by
        the count check it already had, with no special case.
      * `client_impl.cpp`'s duplicate `register_all_known_entity_types()` call
        went out with the function, as planned. No separate fix needed.
      * **`add_entity` takes `const entities::Entity*`**, not the map's
        `shared_ptr`. What it needs is bytes to copy from, and taking the owning
        handle is what tied session storage to an editor allocation decision — the
        `map_entity_t` question is now free to move without touching this file.
        One call site gained a `.get()`.
      * **The type-mismatch branch could not survive**: `add_entity` picks the
        pool BY `entity->type`, so the mismatch `add_existing` used to report
        through `invalid_entity_slot` is unreachable from there. `push_copy`
        asserts as a backstop rather than as a path.
      * **One coupling was load-bearing and unchecked**, found while writing it:
        the pool strides by `entity_info(type).size_in_bytes` while
        `entities_of<T>()` walks by `sizeof(T)`. They agree by construction — the
        generated column IS `(uint32_t)sizeof(T)`, read rather than assumed — but
        a disagreement leaves element 0 correct and everything after it garbage,
        which is the worst available failure mode. `entities_of<T>()` asserts it
        now, and the assert was **watched fire**: stride skewed by 4,
        `session_test` aborts naming the entity and the reason. A second assert
        covers over-alignment, since the buffer comes from `operator new` and
        that promises only fundamental alignment.
      * `validate_locations` lost its whole pool-existence half — an array of
        values cannot be missing an entry — and kept the half that matters: index
        vs. pool agreement.
- [x] **4. The prose.** `make_entity_pool` came off the
      sanctioned-exhaustive-switch lists in `CLAUDE.md` and
      `src/shared/entities/README.md`. The other four stay: they dispatch
      *behaviour*, which is where a switch earns itself.
      * Both lists now also say **storage is not among them**, which is the
        useful half — "adding an entity" is exactly when someone goes looking for
        the place to register it. `README.md`'s "there is no step where you
        register anything" is now true of storage too, instead of true of
        everything except storage.

**A stale claim corrected while closing this** (it had been in both `todo.md` and
`entity_system_def.md` §5): the guard for the storage swap is **`session_test`**,
which calls `validate_locations()` four times — around the uid index, across a
swap-and-pop removal, and after `reset()`. It is *not* `ecs_test`, which both
documents credited. `ecs_test` exercises `shared/old_ideas/ecs.hpp`, an unrelated
component registry that `Entity_System` does not use and this phase never
touched.

**The test suite became one command during this phase.** All 19 tests are
registered with CTest at the bottom of `CMakeLists.txt` (`GAME_TESTS`), each with
`WORKING_DIRECTORY` pinned to the project root: `ctest --test-dir cmake_build -j8`,
~2s. That retires the standing "`map_migration_test` must run FROM THE PROJECT
ROOT" footgun rather than documenting it a third time — verified by running the
suite from `%TEMP%`. Running the binaries directly out of `cmake_build/bin/` is
unchanged and still needs the project root. The list is written out rather than
globbed on purpose: a glob over `bin/` also catches `MyGame`, `def_gen` and
`map_convert`, and the exclusion list that would keep them out rots faster than
the inclusion list. Adding a test means adding the target AND its name.

---

# CVAR TRACK — def_gen  ✅ DONE (steps 1–6, 2026-07-29 → 2026-07-30)

console variables and commands moved to the same DSL + generator the entities
use. `cvar_def.md` is the design (source of truth). This track also renamed the
generator `entity_gen` → **`def_gen`**: its real identity is the schema
compiler (it already emitted the asset manifests, and P8 has it absorbing
messages).

**What it fixed.** `game_shared` is a STATIC lib linked into both
`game_client.dll` and `game_server.dll`, so anything with static storage in it
existed *twice*. The old `CVarSystem` singleton was therefore two singletons:
`spawn_bot` registered in one registry and executed against another, and
`cl_timescale` slowed rendering but not simulation. There is now no
registration and no static initializer at all — a cvar read is a field access
(`cvars.pm_maxspeed`), names exist at runtime only in the console, and a
missing or wrongly typed command handler is a **link error naming the symbol**.

**Wire artifact for P8:** `S2C_CvarValues` is already bitstream-native, so P8
has nothing to convert. `S2C_CVarSync` and `CvarPair` are deleted from
`game.proto`, making P8's conversion list two messages shorter than written.

- [x] ~~1. Write `cvars.def`~~ — **done 2026-07-29**, at
      `src/shared/cvars/cvars.def` (generated pair in a sibling `generated/`,
      mirroring `src/shared/entities/`). 22 cvars + 5 commands.
      `@Cheat`/`@Admin` dropped (no declaration ever used either); `map`
      converted from cvar-with-callback to `@Server` command; `Replicated` →
      `@Mirrored` (the 10 `pm_*`/`g_gravity`, and nothing else). Audit
      findings recorded in the file:
      * **`r_fov` is read by nothing** — declared 3× (all launchers, including
        the dedicated one), every FOV in code is a literal
        (`renderer.cpp:3034`, `camera.hpp:189`). Kept, unread; wiring it is a
        one-liner at those two sites.
      * **`cl_timescale` is a live instance of the cross-DLL bug** — read by
        `main_integrated.cpp:92` (scales the SERVER accumulator, exe's copy)
        and `client_impl.cpp:142` (scales client dt, DLL's copy). console
        setting reached only the client's, so slow-mo slowed rendering but not
        simulation. Left unflagged; step 3 fixed it structurally.
      * `sv_tickrate` was `flags::None` and is now `@Server` (only server code
        reads it; the client learns tickrate from `CmdAccept`, a handshake
        fact, not a mirrored value).
      * `debug_show_collisions` gates recording in SHARED collision code while
        drawing is client-side — unflagged, and step 3 makes the integrated
        console toggle finally reach the simulating side.
      * **`string<N>` has zero users in v1** — `map` was the only string cvar.
- [x] ~~2. Rename `entity_gen` → `def_gen` and extend~~ — **done 2026-07-29**.
      `src/tools/def_gen.cpp`, CMake target `def_gen`, all doc refs updated.
      `cvars`/`commands` block kinds with mandatory description literals;
      emits `src/shared/cvars/generated/` (`cvars_generated.{hpp,cpp}` +
      `server_command_bindings.cpp` + `client_command_bindings.cpp`);
      `--dump` lists cvars/commands. Decisions made while building it:
      * **One run, every `.def`.** The tool takes N inputs, and `SCHEMA_HASH`
        is folded across all of them (`mix_schema_hash`). CMake passes both
        files to one custom command. A partial run writes a hash that
        disagrees with a full build — that's why `--emit` is opt-in and the
        usage text says so.
      * **Output dir is derived** from each input's path
        (`<def_dir>/generated`), since with N inputs one `--output-dir` means
        nothing. `--output-dir` survives as a single-input override.
      * **Families are fenced.** One `.def` holds one family; mixing is a
        hard error (they emit different artifacts into different
        directories). Flag vocabularies are disjoint and neither falls back
        to the other — `@Networked` on a cvar and `@Client` on an entity
        field both error.
      * **Block names are excluded from the hash**, so renaming a section
        doesn't refuse every connection. Descriptions and defaults are
        excluded too: the hash answers "do the two builds agree about what
        the bytes mean".
      * **`string<N>` implemented after all** (not deferred): ~20 lines,
        `pascal_string_t<N>` with the zero-padding invariant restored on
        write, verified by compiling a fixture. Still zero users in
        `cvars.def`.
      * **Commands are `TYPE_VOID` field records** in the same IR array — a
        command is the same shape as a cvar line minus the value.
      * Bool parsing is a **closed set both ways**: the old `CVar<bool>`
        mapped anything not in `1/true/yes/on` to FALSE, so
        `debug_show_navmesh tru` silently turned it off. Unrecognised text is
        a rejection and the value is left alone. Numeric parsing requires the
        WHOLE token (`pm_maxspeed 320abc` is rejected, not read as 320).
      * `SCHEMA_HASH` stays a single symbol in `entities::` — the cvar header
        deliberately does not emit a second one. The namespace is now a
        slight misnomer; renaming it touches the handshake, so it waits.
      * Folded in the two **generator polish** items: `field_info_t`'s
        sentinels are named (`NOT_A_COMPONENT` / `NOT_A_STRING` /
        `NOT_AN_ASSET_CLASS` / `NOT_AN_ENUM`, compared by name in
        `entity_reflection.cpp` and `entity_layout_test.cpp` instead of
        `>= 0`), and all generated tables emit designated initializers.
- [x] ~~3. Ownership cutover~~ — **done 2026-07-29**. All 18 tests green;
      dedicated server boots. Every `CVar<T>` global gone, `cvar.hpp` included
      by NOTHING. Launcher owns `cvar_state_t` + `command_table_t` (all three
      launchers), threaded through `client::init(state, table)` /
      `server::init(state, table)` onto `client_context_t::cvars` /
      `server_context_t::cvars`. Decisions made while doing it:
      * **`execute_console_line` was pulled forward from step 4.** Step 3
        deletes the registry, so leaving `CVarSystem::Execute` in place would
        have meant a commit where every console command silently resolves
        nothing. It is also the natural consumer of the cutover: the console
        and the server's remote-command inbox call the same function, so a
        line typed locally and the same line off the wire take one path.
      * **Ownership is decided by `forward_to_server`, not by a build flag.**
        A networked client installs it on connect (`enter_connected_phase`),
        so `@Server`/`@Mirrored` names forward; a dedicated server and a
        disconnected client leave it null.
      * **Shared readers take the state as a parameter.**
        `player_move(const cvar_state_t&, ...)` and
        `audio_system_t_t::init(const cvar_state_t&)`. A reference makes
        client/server agreement about the `pm_*` values a signature obligation
        rather than a hope about which copy of a static-lib global each module
        linked — the same argument the `@Mirrored` flag makes.
      * **`record_collision` no longer checks its own flag** — it records
        unconditionally and `player_move` gates the call, reading
        `debug_show_collisions` ONCE per tick so a mid-tick toggle can't
        record half a frame.
      * `Play_State::conn_state_` deleted — it existed only to feed the old
        capturing forwarder lambda, and a written-never-read member is not
        something the compiler would have flagged.
- [x] ~~4. Wire — mirrored-values message~~ — **done 2026-07-30**.
      `S2C_CvarValues`, bitstream-native, in
      `src/shared/network/cvar_mirror.{hpp,cpp}`: `count` then `(cvar_id, text)`
      pairs. Full set sent per-client right after `CmdAccept`; the per-tick diff
      is `collect_changed_mirrored_cvars` (memcmp of the value bytes at each
      `cvar_info`'s offset/size against `server_context_t::last_broadcast_cvars`)
      broadcast at the END of `Tick`. `S2C_CVarSync` + `CvarPair` gone from
      `game.proto`, along with the `Message_Type` entry, the `Packet_Traits`
      specialization and the `cvar_syncs` inbox vector. Decisions made while
      doing it:
      * **Values ride as TEXT, not raw bytes.** `cvar_to_text`/`cvar_from_text`
        stay the only place cvar bytes become characters — the same argument
        `entity_reflection` makes for field text. Floats use the shortest
        round-tripping representation, which `cvar_test` pins as BIT-exact: a
        mirrored `pm_*` that decoded to a near value would drift the client's
        prediction away from the server's simulation one sync at a time.
      * **The retain happens only after the send.** A change that is never
        broadcast stays different from the retained copy and is collected again
        next tick — that IS the lost-update repair, and there is no ack.
      * **The receiver refuses anything not `@Mirrored`**, and an out-of-range
        id is `log_error`, not a clamp. Both can only mean the two builds
        disagree about `cvars.def` despite a matching `SCHEMA_HASH`.
      * **Not gated on `client_map_ready`** (unlike snapshots): a cvar value is
        world-independent, and a client mid-download wants the movement
        constants it will simulate with the moment its map lands.
      * The broadcast sits at the END of `Tick` so it catches every writer —
        a console line off the wire, a command handler, gameplay code writing
        the field directly.
- [x] ~~5. Delete `src/shared/cvar.hpp` outright~~ — **done 2026-07-30**, and
      dropped from `CMakeLists.txt` and `meson.build`'s source lists.
- [x] ~~6. `cvar_test`~~ — **done 2026-07-30**, `src/test/cvar_test.cpp`, all
      green (19 tests overall). Covers the generated tables
      (name/description/side coverage, the one flat namespace,
      `mirrored_cvars()` vs the flag), text conversion (bool as a closed set
      BOTH ways, partial numeric parses rejected, float bit-exact round trip),
      the console dispatcher (read/write/errors, forwarding decided by
      `forward_to_server`), the generated argument binders (defaults, enum by
      name, the `string...` tail keeping interior whitespace, usage strings),
      and the mirroring path end to end.
      * It compiles BOTH generated binder TUs and supplies its own
        `commands::<name>` handlers — the only way to reach the binders outside
        the DLLs that own the real ones, and a standing check that a binder TU
        references nothing but those handler symbols.
      * **ACCEPTANCE MET**: `spawn_bot` typed by hand into `MyGame.exe`'s ImGui
        console → `spawn_bot: spawned idle bot at slot 32`, each command logged
        exactly once. `spawn_bot` was the bug that started this whole track, so
        that was always the criterion.

### The forward loop — found on the last day of the track

The integrated console had been broken **since step 3**, and was found
2026-07-30 only from a live report of the server log repeating
`Command from slot 0: spawn_bot` forever after one console entry.

`main_integrated.cpp` handed client and server **one** `command_table_t`. The
loopback client installs `forward_to_server` on connect. So the server,
dispatching a line that had just arrived over loopback UDP, saw a `@Server`
command AND a live forwarder — and forwarded it back to itself. Infinite
ping-pong; the handler never ran. Fixed two ways:

- **One `command_table_t` PER SIDE** in the integrated launcher. A table is a
  module's DISPATCH SURFACE (which names it can run, whether it forwards), not
  shared process state like `cvar_state_t`. The values stay shared — that is
  the thing this track exists to share, and conflating the two caused this.
  **The general rule**: when hoisting state into the launcher to escape
  static-lib duplication, ask per member whether the two modules should AGREE
  on it (share one object) or DIFFER on it (one per side, even in one process).
- **A line that arrived from the wire is never forwarded again.**
  `command_context_t::caller_slot >= 0` already means "a network player sent
  this", so `execute_console_line` refuses to forward it. Makes the loop
  unrepresentable rather than merely absent.

`cvar_test`'s `[console: no forward loop]` section is the regression guard.

**Two lessons worth keeping:**

1. An out-of-process probe against `MyGame_Server.exe` **cannot** catch an
   integrated-build ownership bug — the dedicated server legitimately has no
   forwarder, so the loop cannot form there. A probe of exactly that shape
   passed while the integrated build was broken. The integrated build is a
   DIFFERENT topology, not just a convenience; exercise it too.
2. Same disease was still live elsewhere: the asset registry and
   `debug_collision::g_collision_faces` were `game_shared` globals duplicated
   per module. **Both fixed 2026-07-30** — see the MODULE OWNERSHIP TRACK below.

**Known rough edge, deliberately left:** `spawn_bot 1` is rejected with the
usage string. Enum parameters bind by NAME only (`idle|chase|regular`), never
by ordinal — deliberate (`cvars.def`: lowercase values are the console-typed
identity), but it is the first thing a user reaches for. If numeric-or-name is
ever wanted it belongs in the generated binder's enum parse, so every enum
parameter gains it at once.

---

# MODULE OWNERSHIP TRACK — the rest of the static-lib disease  ✅ DONE (2026-07-30)

The CVAR TRACK fixed cvars but named two survivors of the same root cause. This
finished them. Nothing here is a new idea: it is the cvar shape applied twice
more, plus the observation that the shape has **two** correct answers and you
must pick one per member.

**The root cause, once more.** `game_shared` is a STATIC lib linked into the
launcher exe AND both DLLs, so a file-scope global in it exists three times.
The rule that falls out:

> Decide per piece of state whether the modules must **AGREE** on it (one
> object, launcher-owned, pointer handed to each module) or should **DIFFER**
> (one per side, and say so). A global in `game_shared` silently gives you
> "differ" whether or not that is what you meant — which is why every bug in
> this family looked like something else.

## 1. The asset registry — AGREE

**The bug.** All three launchers called `assets::init()` from the **exe**, and
every `get_mesh` caller lives in **`game_client.dll`**. The copy that got filled
was never the copy that got read. Observed twice and not client-specific:
`render_3d` logging `get_mesh called before assets::init()` every frame in
`MyGame_Client` (2026-07-28), and `spawn_cube` producing an invisible cube in
the **integrated** build (2026-07-30).

Worth keeping: `get_mesh` returned an **invalid handle, not the `Missing`
placeholder** — the placeholder lookup sits downstream of the initialized
guard. So the symptom was *nothing drawn* rather than a question mark, which is
exactly why it read as "spawn_cube is broken" for two days instead of as an
asset problem.

**Why the whole state moved, not just the manifest.** `asset_handle_t` is a
bare `uint32_t` index into a pool's `items` vector. Sharing the manifest alone
would hand over handles that index into the *launcher's* pool while the client
dereferenced its own empty one — a subtler version of the same bug. The pools
and the manifest are one unit or they are nothing.

- [x] `assets::asset_state_t` (in `asset.hpp`) holds all of it: the three
      `Asset_Pool`s and the manifest handle arrays. `Asset_Pool` moved into the
      header so the launcher can own the state **by value**, exactly like
      `cvars::cvar_state_t`.
- [x] `assets::set_state(asset_state_t*)` points **this module's** accessors at
      it. One pointer per module, because that is what a static lib gives you;
      the point is that all of them aim at the same object.
- [x] `client::Init` / `server::Init` take an `assets::asset_state_t*` beside
      the `cvar_state_t*` they already took, and call `set_state` before
      anything resolves an id. All three launchers own the state and pass it.
- [x] Every accessor resolves through `state_for(name)`, which **log_errors
      naming the caller** if the module was never pointed at a state. The old
      failure mode was silence; this one is loud (no-silent-failures).
- [x] `asset_test` owns a state like a launcher does — it IS the launcher.

**Verified at runtime, not just by the suite.** The 19 tests link `game_shared`
directly and never cross a DLL boundary, so they could not have caught this and
did not: they passed before the fix. The real evidence is the integrated build
logging `[renderer] Uploaded mesh 0: 540 verts, 3264 indices` and **zero**
`get_mesh called before assets::init()` across a run that rendered and
simulated, where that error previously appeared every frame.

Consequence worth noting: the `Missing` placeholder path is *reachable* again,
so an unassigned mesh field now draws the question mark by construction. This
is how `Rocket_Entity` renders today (`mesh='Missing'`) — a content gap that
was previously masked by drawing nothing at all.

## 2. `debug_collision::g_collision_faces` — DIFFER

Same global, opposite answer. Once `debug_show_collisions` genuinely reached the
server (CVAR TRACK), the server recorded faces into its own copy that nothing
drew and nothing cleared; `server::Tick` had grown a per-tick drain purely to
stop it growing without bound.

The faces are **not** shared state — they are one side's view of its own
simulation run, and the server's were never drawable (the drawing is
client-side; in a networked build they would have to cross the wire to be seen
at all). So the sink became a parameter and the global was deleted rather than
shared.

- [x] `debug_collision::Face_Sink` (a `std::vector<Debug_Collision_Face>`), owned
      by the client on `client_context_t::debug_collision_faces`.
- [x] `player_move(..., Move_Events*, Face_Sink*)` — the sink follows the
      `out_events` side-channel pattern that was already there. Null means record
      nothing, which is how the server and the bot system opt out.
- [x] `g_collision_faces` and `clear_collision_faces()` deleted, and with them
      the drain in `server::Tick` — there is no longer anything to drain.

## 3. `bot_debug::g_entries` — found during the audit, dead

A third `game_shared` global with the same shape. Its header called itself "a
lightweight debug bridge between game_server and game_client", which a static-lib
global cannot be. The server's per-tick fill in `bot_system.cpp` was a **pure
dead store**: `server_impl.cpp` serializes `S2C_BotDebug` from `g_bots`
directly, and the client fills its own copy from the wire. Nothing was visibly
broken — the wire was doing the work the global claimed to do.

- [x] Deleted the server-side fill; corrected the header to say what it is
      (client-owned, wire-fed, works in both integrated and networked builds).

**The lesson to carry.** All three were found by asking one question of every
`game_shared` global — *must the modules agree on this, or differ?* — rather
than by observing a symptom. Two of the three had no visible symptom at all.
The remaining cross-module `extern` data in `game_shared` after this track is
`bot_debug::g_entries`, which is now correctly client-only.

---

# PLAYER DIMENSIONS + RENDERER PASS  ✅ DONE (2026-08-04)

Four unrelated fixes that landed the same day, moved out of `todo.md`.

## The player was three different sizes depending on who was asking

All three now derive from the movement hull — `player_half_width = 16`,
`player_half_height = 36` (`player_constants.hpp`), i.e. **32 x 32 x 72**,
HL/Source exact.

- **Hitbox table** — `player_hitboxes.hpp` tiles 0..72 exactly: legs 0-30,
  torso 30-54, head sphere centered 63 r9. Half-widths stay inset from the
  hull's 16 and never exceed it. `hitscan_test` moved with it (its region
  probe heights are the table's centers).
- **Jolt kinematic capsule** — was radius 18 / cylinder half-height 20
  (36 x 76) written out at five call sites. Now three derived constants
  (`player_capsule_radius`, `player_capsule_cylinder_half_height`,
  `player_capsule_center_offset`) that all five use, giving 32 x 72. Note
  Jolt's `CapsuleShape` takes the CYLINDER half-height and adds a cap of
  `radius` at each end, so the cylinder half is
  `player_half_height - player_half_width` = 20, NOT 36 — writing 36 there
  would give a 104-tall player.
- The **bot** capsule was a fourth size (18/38 with a `{18,38,18}` hitbox
  where a human player got `{16,36,16}`) and is now the same expression as
  the player's. A bot hittable where a player is not would have made every
  aim test a lie.
- `Hitbox.size.y` on a Capsule now means the CYLINDER half-height on every
  writer, which is what its two readers — Jolt and `draw_hitbox_capsule` —
  already assumed. Both old values were total half-heights, so drawing a
  player's debug capsule would have shown a 104-tall one. Nothing draws
  players today; this closed it before something does.
- `player_eye_height = 64` (landed 2026-08-03) needed no change: already
  sized for the 72 hull, one definition and three consumers (server hitscan
  origin, bot aim, client camera), and now sits just above the head sphere's
  center rather than near its lower edge.

Real behaviour change: rocket splash and direct hits use a body 4 units
narrower and 4 shorter, and headshots land lower.

## The editor stored `Player_Spawn_Entity` positions as the hull CENTER while the runtime read them as FEET

Code and map data both fixed. `compute_placement_center` added
`half_extents.y` to the clicked surface point and stored that as `position`,
so every editor-placed spawn was written 36 units — exactly
`player_half_height` — above the floor, and players spawned in the air and
fell. The editor was self-consistent (it also *drew* and *picked* centered),
which is why it looked right and only the runtime disagreed.

- Code: `player_hull_bounds` (`map.cpp`) and `draw_player_spawn_shape` are now
  feet-based, and placement gained `get_placement_origin_height` — half the
  height for a centered origin, **0** for the player-shaped types.
  `compute_placement_center` → `compute_placement_origin`, since it no longer
  returns a center; `draw_entity_ghost` / `draw_default_ghost` take the origin
  for the same reason.
- Data: both maps' spawns were at `-1024 36 -896`. Set to the floor top
  beneath each — `other.source` box `_uid 4` tops out at exactly **0**
  (confirming 36 = 0 + `player_half_height`), `new_map.source`'s at
  **2.586742**. `maps/test` and `maps/test_backup` have no spawns, so the
  migration fixture was untouched.
- **The rule this leaves:** an entity whose origin is not at the center of its
  shape needs a `get_placement_origin_height` case, or the editor and the
  runtime will disagree about where it is. Recorded in
  `src/client/editor/readme.md`.

## `set_line_depth_bias` was per-frame, not per-call

The header promised "bias for subsequent draw_line calls", but `draw_line`
batches and `flush_lines` drew the whole frame with one `vkCmdSetDepthBias`
taken from the globals at flush time — so the LAST value set in a frame
applied to every line in it.

- Live consequence: `entity_editor_traits.cpp:329` set `-200` to pull a
  selection outline in front of the solid mesh, then restored `-2` before
  returning. The restore always won, so **the -200 never took effect once**
  and selection outlines z-fought.
- Half-working in a way that hid it: `draw_mesh(wireframe=true)` is immediate
  and applies the bias correctly at call time, so a selected entity WITH a
  mesh outlined correctly and one falling back to wire boxes/wedges did not —
  the same feature behaving two ways depending on the entity.
- Fix: the line batch is a list of `line_run_t` split at each bias change
  (`renderer.cpp:907`), one `vkCmdDraw` per run. Splitting happens at first
  `draw_line` after a change, not in `set_line_depth_bias`, so a bias set with
  no lines after it costs nothing. Typically 1–2 draws.

## `render_view` did not render a view

It set three globals (`g_current_view_proj`, `g_camera_right/up`) and
returned; its `ecs::Registry` parameter was entirely unused (`(void)registry`
under a TODO) and all three call sites built an empty `ecs::Registry reg;`
just to feed it. Renamed `set_view(cmd, view)`, registry parameter dropped,
three dead locals and the `old_ideas/ecs.hpp` include deleted. This also
explains a redundancy the name had been causing: `set_view` already calls
`set_viewport`, and two call sites called it again immediately after — nobody
does that on purpose, they do it because the name made the first call
invisible.

---

# ANIMATION TRACK — build-order steps 1–2  ✅ DONE (2026-08-08 → 2026-08-09)

Design and the remaining work are in `animation_def.md`; its "WHAT'S LEFT"
section is the live list. This is what landed and the reasoning worth keeping.

## Step 1 — exporter, loader, model on screen  (2026-08-08)

`src/tools/blender_export.py` emits `.skeleton` + `.mesh`;
`src/shared/model_format.{hpp,cpp}` reads both; `src/shared/skeleton.hpp` holds
`skeleton_t` / `bone_t` / `vertex_skin_t`. `assets::load_mesh` dispatches on the
`.mesh` extension — it must, because `load_obj` normalizes to a 100-unit max
extent and a `.mesh` is already in engine units.

**`model_format_test` checks the REAL exporter output, not only fixtures.** The
exporter's promises and the reader's assertions are the same list, so that is
where the two are made to meet.

**The axis conversion and the scale were proven without a renderer**, which was
the point of doing it this way: a standing humanoid is not an ambiguous shape.
Vertical span 68.6 units against a depth span of 12.8 (so Y is up — Blender's
Z-up would have swapped those), feet near the ground plane, ~67 units against
the 72-unit player hull (so the 39.37 metres→units factor survived). Asserted in
`test_real_mesh`, deliberately as ranges rather than exact values, so legitimate
model edits do not turn the test red.

`mesh_asset` in `entities.def` declares TWO scans — `resources/obj/*.obj` and
`resources/models/*.mesh` — so a skinned model is an ordinary manifest id that
the editor can place and `draw_mesh` renders. `def_gen`'s `scan` directive
became repeatable for this; scans expand in declaration order, files sorted
within each, so ids stay deterministic.

**Players and bots draw the model — the missing half of the original plan.**
`Player_Entity::render` was declared, `@Networked`, and completely reader-less,
so remote players were a green collision AABB and nothing else.
`server::initialize_player_body` (`entity_lifecycle.{hpp,cpp}`) now assigns the
hitbox and the model in ONE place called by both `spawn_player_for_slot` and
`spawn_bot` — a bot IS a `Player_Entity`, and the hitbox half of that setup had
already drifted across the two before the model gave it a second reason to.

The client draws it in `play_state.cpp` from `latest_player_entities` (where the
snapshot puts the Render component) at `Remote_Player_State::render_position`
(where interpolation happens) — neither container has both. `cl_draw_player_hull`
survives as a debug overlay rather than being retired: the hull is where a player
COLLIDES and the model is only what you see, so drawing both is how you catch a
model that has left its own hull.

**Bots are the test vehicle for every step after this** — the only way to see a
third-person model without a second machine.

## Step 1a — texture rendering  (2026-08-09)

`material_t` gained a resolved `asset_handle_t<texture_asset_t>`, filled by
`load_mesh`. The parser stays a pure function over a file with no pools, so the
path remains the on-disk identity and the handle is what the renderer binds.

**Textured-ness is a property of the MATERIAL, not of the draw call.** A caller
asks for `shader_type::Lit` and each submesh gets whichever lit pipeline its
material earned, so one mesh mixes them. That is why no new `shader_type` was
added — the caller has nothing to say about it.

Three cases, resolved in ONE place (`material_albedo_descriptor_set`) so the
skinned and unskinned paths cannot drift on what a broken material looks like:

- a resolved texture binds it;
- **a material that NAMES a texture that failed to load binds a magenta/black
  checkerboard** — the `mesh_asset::Missing` treatment;
- a material that names none is legitimate (flat colour) and takes the
  untextured pipeline.

Descriptor sets are per TEXTURE ASSET, not per material, so two materials naming
one image share the upload. Textures upload as `R8G8B8A8_SRGB`: the swapchain is
`B8G8R8A8_SRGB`, so the hardware encodes on write and a colour texture sampled as
UNORM would be gamma-corrected twice. `upload_texture` took an `srgb` parameter;
data maps (roughness, metallic, normals) keep UNORM.

**The UV V-flip.** Blender's UV origin is bottom-left with V up; stb_image hands
back the top PNG row first and nothing calls `stbi_set_flip_vertically_on_load`,
so the sampler's V=0 is the top with V down. Fixed in the exporter's
`to_engine_uv`, per the same rule as the axis conversion. **It is NOT the axis
conversion** — a UV has no third component to rotate. It survived this long
because nothing had ever sampled a texture with mesh UVs: `lit.vert` bound `inUV`
and called it unused, and the displacement path generates its own worldspace UVs.

**Exporter: texture copying became unconditional.** It used to branch on whether
the image was packed (copy into `<out>/textures/`) or linked (reference it where
it sits), which is how a shipped `.mesh` came to name
`resources/blender/textures/Image_8.png`. Two things wrong: a runtime asset must
not point into the Blender source tree, and `Image_8` is not a name. Worse, WHICH
branch you got depended on an incidental property of the `.blend`, so one model
exported from two files produced two path regimes silently. `TextureWriter` now
holds both caches — by image pointer (two materials sharing an image share one
file) and by output filename (the collision check). Deriving the output name from
the material means two materials colliding on one filename is a hard `fail()`
naming both, because only the author knows which was meant.

## Step 2 — GPU skinning, bind pose  (2026-08-09)

`src/shared/skinning.{hpp,cpp}` holds the hierarchy walk, in `game_shared` rather
than the renderer for one reason: it is the half of GPU skinning that can be
checked without a GPU. `model_format_test` runs the real 35-bone rig through it
and asserts every skinning matrix comes out identity.

**A correction to the plan's own rationale, worth keeping because it was stated
confidently and was wrong.** `todo.md` and `animation_def.md` both said that
filling the UBO properly — deriving locals from `inverse_bind`, walking the
hierarchy, `model_space[i] * inverse_bind[i]` — would be "the ONLY thing that
will ever check the loader's row-major→column-major transpose". **It does not,
and cannot.** Every matrix in that computation descends from `inverse_bind`, so a
uniformly transposed skeleton telescopes to identity exactly as cleanly as a
correct one: `local[i] = IB[parent] * inverse(IB[i])` makes `model_space[i]`
collapse to `inverse(IB[i])` whichever convention `IB` is in, and the final
multiply cancels it. **Only a pose from OUTSIDE the skeleton can test it** — a
clip or an authored aim pose — so that check belongs to the aim step.

Deriving rather than uploading identity is still right, just for a smaller
reason: it runs the same walk a real pose will. What it does prove is the
multiply order, the hierarchy walk, `inverse_affine`, the second vertex binding
decoding at stride 20, the dynamic-offset UBO, and the descriptor sets.

**Per-draw uniform data got a general mechanism, not a bone-specific one.** Bone
matrices are the renderer's first GPU-resident per-draw data — 8 KB against a
128-byte push constant budget — and `frame_uniform_allocator_t` serves them. Two
policies, deliberately different, and the naming matters because the codebase has
both:

- **WITHIN a frame: a bump allocator.** Draws take blocks off a rising offset,
  nothing is freed individually, the whole region resets at frame start.
- **ACROSS frames: double buffered, NOT a ring.** One region per frame-in-flight;
  `begin_frame` has already waited on that index's fence, which is what makes the
  reset safe. (The line batch and the debug face buffer are the actual ring
  buffers here — head/tail with wraparound. This is deliberately not one.)

**One skinned pipeline, not two.** Skinned × textured is a permutation axis, and
a fourth pipeline would buy a case no asset has — a skinned mesh is a character
and characters are textured. An untextured material on a skinned mesh binds a 1×1
white texture instead. Albedo stays at set 0 and bones take set 1, so
`lit_textured.frag` is reused byte for byte by both pipelines: only the vertex
half of a skinned draw differs, and the set numbering says so.

**`skinning_matrices == nullptr` on a skinned mesh means BIND POSE**, derived from
the skeleton and cached per skeleton handle. A real default rather than a failure:
no call site changed for this step, so the animation work can reach them one at a
time.

## Step 7 — aim: the authored pose set, driven by view angles  (2026-08-09)

Promoted out of order (it is build-order step 7) because it was the only step
whose data already existed and it needs no animation authoring skill. Design in
`animation_def.md` §5.

**The pipeline.** `blender_export.py` gained `--poses`, which appends every
Action out of `resources/blender/asset_library/*.blend`, evaluates the rig and
writes a **single-frame `.animation`** per pose. One format, one loader, one hash
check — there is deliberately no `.pose`. `models::parse_animation_file` reads
them; `assets::load_animation` resolves the sibling `.skeleton` and refuses a
hash or bone-count mismatch, the same shape as the `.mesh` check.

`src/shared/animation.{hpp,cpp}` is the runtime: `transform_t` / `pose_t`,
`sample_animation_clip_at`, `blend_into` with float per-bone masks, `get_local_transforms_of_bones_from_pose`,
`compute_bind_pose`, `build_bone_mask`, and the aim blend. `linalg` gained
`quatf` (nlerp with the hemisphere fix, `to_mat4`, `compose_transform`) — the
rotation half of a pose, and the only reason a pose is TRS rather than a matrix.

`src/client/player_animator.{hpp,cpp}` is the client half; `play_state.cpp`
drives it from `render_pitch` and the torso twist.

**Three things worth keeping:**

**1. Headless, an armature's pose is NEVER EVALUATED in Object mode.** Assigning
the five actions in turn produced five `.animation` files that differed only in
their name line — a whole set of well-formed files full of plausible numbers,
the same failure shape as the Rest Position trap through a different door. Set
`pose_bone.location`, then `update_tag()`, `view_layer.update()`, `frame_set()`
and `depsgraph.update()` in every combination: `pose_bone.matrix` does not move,
on the original OR on `evaluated_get()`, though `matrix_basis` plainly carries
the change and the rig is in the depsgraph. Toggling through **Pose mode** is
what makes the recompute happen. The exporter now enters it and then
`verify_poses_differ` refuses to write a set where any pose equals rest or two
poses are identical; `model_format_test` checks the same thing on the shipped
files, because the failure is silent everywhere else.

**2. Entering Pose mode invalidates every `Bone` reference collected before it.**
Reading `.name` off one afterwards comes back as a `UnicodeDecodeError` on
whatever now occupies that memory. The pose path takes bone NAMES, which are
already the stable identity `skeleton_hash` is built on.

**3. `Asset_Pool::items` became a `std::deque`.** `assets::get` hands out a
pointer INTO it, so with a vector

```cpp
const animation_clip_t *first  = get(load_animation(a));
const animation_clip_t *second = get(load_animation(b));   // `first` dangles
```

was UB with no symptom at the call site — the freed memory still reads as a
plausible asset. It surfaced as five aim poses comparing equal to each other
after loading in a loop. A deque never moves an element already in it, which
makes the natural way to write that correct. Nothing removes from a pool, so the
other invalidation rule cannot arise.

**Aim is a PLUS, not a square, so "bilinear" was not achievable literally.**
There is no up-left pose, hence no four corners. `compute_aim_blend` is
barycentric on the plus: one vertical neighbour, one horizontal neighbour, and
Forward taking the remainder, then normalized. It degrades to a plain two-pose
lerp on either axis alone (the common case) and at a full diagonal splits both
extremes evenly — which two sequential `blend_into` calls would not, since the
second would overwrite the first.

**The body is drawn at `body_yaw`, which LAGS the view yaw.** Not in the design
doc's bullet list, and load-bearing: with the model drawn at the view yaw the
torso can never be turned relative to it, so the left/right poses are
unreachable by construction and half the pose space is dead. The feet chase the
view at `cl_aim_body_turn_rate` and are pushed round once the twist would exceed
`cl_aim_max_yaw`; the leftover deviation is what the poses cover. Purely local
tier-2 state, never replicated.

**The transpose finally got tested.** `test_bind_pose_skinning_is_identity`
cannot see a uniformly transposed `inverse_bind` — every matrix in it is derived
from `inverse_bind`, so a wrong transpose telescopes to identity just as
cleanly. A pose arrives as TRS, where translation is translation with no
row/column ambiguity to cancel against, so
`test_posed_skinning_stays_a_person` skins the real mesh through each of the
five poses on the CPU and checks the result is still person-sized and on the
ground. That is the check `animation_def.md` §7 said only an authored pose could
make.

**Found while verifying, not fixed:** `left_holding_gun` has the legs swapped —
`DEF-foot.L` sits where `foot.R` is in the other four poses. Confirmed against
Blender directly, so it is authoring (a paste-X-flipped pose that caught the leg
controls), not export. Blending toward it swings the legs around.

## Blender facts that cost real time — don't rediscover them

- **A `.blend` saved in EDIT MODE** has stale flat arrays and can have an EMPTY
  UV layer while looking fine on screen. Every downstream symptom is
  unrecognisable as that cause. The exporter now refuses by name.
- **What the viewport shows is not what the exporter reads.** The viewport draws
  the EVALUATED mesh (rest geometry + modifiers + the armature's current pose);
  the exporter reads `mesh.vertices[i].co`, which is rest. A model can stand on
  the floor on screen while its rest geometry sits below it, with every object
  transform at zero — the displacement lives entirely in bone pose, which the
  Object properties panel does not report. Toggle Pose Position → Rest Position
  to see what the exporter sees.
- **The mesh and skeleton export is entirely pose-independent** (verified by
  diffing exports across a pose change: mesh byte-identical, skeleton identical
  to 1e-5 rounding). Positions, normals, UVs, weights and `bone.matrix_local`
  are all rest data. Pose export is the part that is not, which is why it
  asserts `pose_position == 'POSE'`.
- **A rig's POSE IS NOT EVALUATED headless while the object is in Object mode**,
  and nothing about that fails loudly — see Step 7 above. `mode_set('POSE')`
  first; every `Bone` reference collected before it is then dangling.
- **Blender is not on PATH:**
  `& "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe" <file>.blend --background --python <script>.py -- --out resources/models`
  Run it from the project root — texture paths are relativised against cwd.

---

# HIT FEEDBACK + AIM DEBUG RIG  ✅ DONE (2026-08-09)

Started as "make an idle bot rotate so I can see whether interpolation works"
and turned into three unrelated fixes plus the damage-feedback pass. Grouped
here because it is one sitting, not because it is one subject.

## The bot yaw convention was wrong in two ways at once

`update_bots` ended every tick with `view_angle_yaw = atan2(front.x, front.z)`.
Two bugs stacked:

- **Radians into a degrees field.** `view_angle_yaw` is DEGREES everywhere else
  — `direction_from_angles`, the proto viewangles, `advance_body_yaw`, the aim
  poses. A bot was writing raw radians, so every bot model rendered facing about
  1.57° off +X instead of 90°.
- **Transposed arguments.** `direction_from_angles` sweeps yaw from +X toward
  +Z, so the inverse is `atan2(z, x)`, not `atan2(x, z)`.

The client's bot debug arrow read the field back with raw `sin`/`cos`, i.e. in
the same wrong convention, which is why the arrow looked right while the model
did not. Both now go through `direction_from_angles`. `front` also defaults to
the bot's CURRENT heading rather than `{1,0,0}`, so a bot dropping to the Idle
goal holds its facing instead of snapping to +X.

An idle-bot spin was added here and then removed once it turned out not to test
what it looked like it tested (below). The convention fixes are the part that
stayed.

## Remote player yaw lerped the long way round the seam

`render_yaw = a*(1-t) + b*t` on an angle. Crossing ±180 whips the model a full
turn backwards over one snapshot interval. Now lerps the wrapped delta
(shortest arc). Found while building a spinning bot, which crosses the seam once
per revolution — it would have read as an interpolation bug that wasn't one.

**Still open, deliberately not fixed:** `ctx.interpolation_time = 0.f` is reset
INSIDE the per-player loop in `play_state.cpp` but the timer is shared across all
remote players. With several players the last one processed wins and everyone
else's `t` restarts mid-interval. Test interpolation with exactly one remote
player until that is sorted.

## The aim pose blend was unreachable from gameplay

The blend takes exactly two inputs, and both were structurally pinned at 0:

- **Pitch** — nothing in the bot path ever writes `view_angle_pitch`. The
  up/down poses could not be reached at all.
- **Yaw deviation** — `advance_body_yaw` creeps the feet toward the view and
  SNAPS the deviation to exactly zero once it is within one step. At 60fps the
  step is `cl_aim_body_turn_rate/60` = 9°, so anything turning slower than
  540°/s has its deviation zeroed every single frame. Turning faster does not
  help either: past `cl_aim_max_yaw` the clamp branch drags `body_yaw` along and
  pins the deviation at a static ±45°, which is one frozen extreme, not a sweep.

So there is no gameplay situation that sweeps the pose space, and no amount of
bot behaviour would have produced one. `cl_aim_debug` forces the pair directly
for every remote model, with an ImGui panel of two sliders — the question is
whether the interpolation is SMOOTH, which only a continuous sweep answers.
Slider range runs to ±90 against an authored extent of ±45 on purpose: past the
extent the blend should clamp to the extreme pose, and watching it stop moving
is the second thing worth confirming.

`cl_spectate_slot <n>` rides a remote player's eye, reading the same
`render_position`/`render_yaw` the model is drawn from so there is no separate
camera path to smooth over a stall. `cl_player_unlit` skips the sun on player
models — half a character sitting in 0.15 ambient is the wrong light to judge a
pose by.

## `unlit_textured_skinned`

A new FRAGMENT shader only: `lit_textured.frag` with the sun removed, declaring
a subset of the vertex stage's outputs (legal, and it lets
`lit_textured_skinned.vert` pair with both frags byte for byte). Two pipelines
now share one layout — same sets, same push constants — hence
`g_lit_textured_skinned_pipeline_layout` → `g_skinned_pipeline_layout` and
`create_lit_textured_skinned_pipeline` → `create_skinned_pipelines`.

**The bug it exposed:** the `skinned` predicate in `draw_mesh` accepted only
`shader_type::Lit`, so a skinned mesh asked for Unlit silently fell to the
unskinned pipeline and drew in BIND POSE. Unlit now takes the Lit branch of
`push_and_draw`, because its vertex stage is `lit_textured_skinned.vert` and it
needs `LitPushConstants` and the skinned layout, not the flat aabb pair.

## `-Werror=switch`, and the half-added event it would have caught

`PLAYER_DAMAGED` was in `game_event_kind_t` with a payload struct and a union
member, but **no serialize case, no deserialize case and no consumer**. Firing
it would have written a bare 16-bit kind and desynced the read cursor for every
event after it in that batch — silently. Clang had been warning twice on every
build for months; nothing made anyone read it.

Removed rather than completed (the damage feedback below needs neither half),
and `-Werror=switch` / `/we4062` added so the next one cannot get that far. The
exhaustive switch is the safety net CLAUDE.md leans on repeatedly; as a warning
it was not one.

**Placement matters:** the flag sits BELOW the FetchContent blocks.
`add_compile_options` only reaches targets declared after it, so SDL2, protobuf
and miniaudio keep their own warning policy and a third-party switch cannot
break our build. Putting it at the top of the file does poison them.

## Damage feedback: which channel, and why not `inflict_damage`

The question was whether damage should dispatch a cosmetic event. Answer: the
two sounds want DIFFERENT channels, and neither belongs in `inflict_damage`.

**Not in `inflict_damage`,** for three reasons that are all still true:

1. It does not know where the hit landed. `damage_info_t.source_position` is the
   shooter's EYE, so a spatialized sound from there plays at the shooter.
   `hitscan_result_t` has `impact_point` and `impact_normal`; the call site was
   dropping both.
2. Fan-out. One rocket detonation is N `inflict_damage` calls, so a blast in a
   crowd would stack N flesh sounds. `damage.hpp` had already made this exact
   call for `ROCKET_EXPLOSION`.
3. It loses the region — only the `was_headshot` bool survives, not
   `hit_region_t`.

**The world thud is a cosmetic effect.** `FLESH_IMPACT`, dispatched at the
hitscan site — the branch that previously produced nothing at all, so hitting a
wall made a noise and hitting a person made none. Appended to `effect_type_t`
rather than slotted next to `BULLET_IMPACT`: the wire id IS the enum value and
nothing hashes that enum, so reordering silently remaps every effect for a peer
on another build. A headshot is louder, not different — a distinct headshot
sound broadcast to everyone announces to the whole server that someone just got
clipped in the head.

**The hitmarker is replicated state.** `Player_Entity::last_hit_tick` +
`last_hit_was_headshot`, latched on the SHOOTER, watched by
`hit_confirm_audio.cpp` exactly the way `weapon_fire_audio.cpp` watches
`last_fire_tick`. Two reasons it is not an effect, and the second is the one
that generalises:

- **Audience.** The cosmetic queue is drained into EVERY outgoing snapshot. A
  hitmarker is per-viewer by definition; the channel cannot express that.
- **Loss.** Effects ride their packet once with no resend. A confirmation you
  never got is lost information, not lost decoration, so it rides the delta
  against the acked baseline instead. Same argument as `last_fire_tick`.

That pair — audience and loss tolerance — is the decision rule for every future
feedback question. It is restated at the top of `hit_confirm_audio.hpp`.

**Content placeholders:** the flesh sounds are `knife_hit1-4.wav`, the only wet
impacts in `resources/sounds`. Only headshots ding; a body hit is already
audible as `FLESH_IMPACT` at the victim, and a second sound on every body shot
turns the common case into noise. The hook for a body hitmarker is one `else`.

**Parked:** generating both event families from an `events.def` — todo.md item
D. The trigger is wanting per-kind cosmetic payloads or the event count roughly
doubling, NOT the enum bookkeeping, which `-Werror=switch` now covers.

---

# THE ANIMATION TOOL, PHASES A AND B  ✅ DONE (2026-08-10)

Phase A is the preview: the model at the origin, a pose picker over bind and the
five aim poses, pitch/yaw sliders through the *real* `compute_aim_blend` /
`sample_aim_pose` path, the skeleton as bone lines. Phase B is the hit volumes
on top of it. Design in `animation_def.md` §4 and `todo.md` §2e; both were
unblocked when aim shipped, because five authored poses that move the spine,
arms and head are the posed content the tool needed to be judged against.

## What phase B actually added

`resources/models/rig.hitboxes` — ten volumes, three damage regions — plus
`shared/hitbox_rig.{hpp,cpp}` (types, resolution, endpoints, derivation, audit),
the reader/writer beside the `.skeleton` and `.mesh` ones in `model_format.cpp`,
and `hitbox_rig_test`.

**A volume is a bone SPAN, and its endpoints are the two named bones' HEADS.**
That is the decision worth keeping: the skeleton stores no tail, so any rule
involving one is a reconstruction that can be wrong, while "name the far joint"
cannot be.

**The shape is named, and the header declares no count.** Both were corrections
to the first cut of the format, and both are the same mistake in opposite
directions. A sphere was originally spelled `start == end` — inference, which
means the format can only express what someone thought of first and which reads
as a typo rather than as a decision; there are now four named kinds (Sphere,
Capsule, Cylinder, Box) in `assets::hitbox_shape_t`. And the file declared a
volume count copied from the `.mesh` and `.skeleton` formats, where it is load
bearing because those are GENERATED and a truncated one would pass silently.
This file is handwritten, so the count was a second thing to keep in step that
could only ever fall out of it. Volumes now run to end of file.

`assets::hitbox_shape_t` is deliberately not `entities::Shape_Kind`. That enum is
the vocabulary of entity hitbox components and Jolt bodies, it has no Cylinder,
and it is switched over exhaustively in the physics and collision paths — adding
a member to serve the rig would oblige four unrelated systems to handle a shape
they cannot make. A Box's half-extents are in the volume's OWN frame (across the
bone, across it, along it), so it turns with the limb rather than being an AABB
that grows whenever the pose rotates.

**The one column the design did not anticipate: `offset`.** The skull is a
single bone whose HEAD sits at the jaw line, so a sphere there covers the neck
and misses the crown; the hands have the same problem at the wrist. `offset` slides both endpoints along the start bone's own
axis, in bone space so it rotates with the pose and the server can reproduce it
from the pose alone — a model-space offset would be a different volume every
time the head turns.

**That axis is minus the THIRD column, not the second.** A Blender bone points
down its own +Y, and `blender_export.py`'s `AXIS_CONVERSION` maps Blender
(x, y, z) to engine (x, z, -y) by conjugation, so +Y arrives here as -Z. The
tool had been drawing leaf-bone stubs along the second column since phase A —
sideways, and never noticed, because a stub pointing the wrong way still looks
like a stub. `assets::bone_direction` is now the one spelling.
`hitbox_rig_test --dump` prints all three columns per bone, which is how it was
caught: every bone's -Z column points at its child's head.

**Derive to seed, author to keep.** The tool is the only place a size is derived
(90th percentile of distance from the axis for the round shapes, and of the
projection onto each of the volume's own axes for a box, over the vertices a
span's bones dominate — end bone excluded so an elbow is not derived twice),
because derivation needs the mesh and the server has none. The table shows both
columns with a fill button between them; Save writes the file. A derived size is
never silently adopted.

## The audit found two things, and neither is a code bug

Both are printed by `hitbox_rig_test` and shown live in the tool, and both are
recorded as loose ends in `animation_def.md` rather than fixed here:

- **`downward_holding_gun` leaves the movement hull.** It bends the spine until
  the head volume sits at chest height, 20 units in front — 9.1 outside, against
  §4's budget of 6. `upward` had the same problem and is now at 4.6.
- **The hands were outside every volume** — 296 of 1216 vertices, because §4's
  volume list stops at the wrist and hands are where the polygons are. Authoring
  two offset hand spheres took coverage to 12 uncovered vertices, all toes. That
  round trip, from a readout to a fix in the same session, is the tool working.

**The tests report these rather than enforcing them.** A per-pose excursion
ceiling would be a test about content that is actively being re-authored, and it
would go red on an improvement as readily as on a regression. What the test does
assert is the shape of the answer: that every volume resolves, that an authored
radius has not drifted into a different order of magnitude from its derived one,
and that nothing lands 40 units off a 32-wide player — which is a broken matrix,
not a lean.

**Still open:** `hitscan.cpp` resolves regions against `player_hitboxes`'s three
static boxes. The capsules pose correctly and the server already links
`game_shared`; wiring them up is the next step and needs no new data.

---

# THE HOUSE FIXED-SIZE ARRAYS  ✅ DONE (2026-08-11)

`todo.md` items A (`rows_in_enum_order`) and B (`Enum_Array`) landed together,
because A turned out to be the thing that makes B safe rather than a piece of
duplication B would absorb.

## What shipped

`src/shared/array.hpp` — three things, no dynamic array:

- `Array<T, N>` — aggregate over `T data[N]`. `uint32_t` size and index, so it
  no longer needs a cast to meet `Span`. Implicit `operator Span<T>` /
  `operator Span<const T>`, because `Span`'s container constructor requires
  `data()` as a CALL and here `data` is a member variable.
- `Enum_Array<Enum_T, Value_T>` — length from `enum_traits<Enum_T>::count`,
  indexed by the enum. `operator[]` unchecked for a key your own code produced,
  `try_get` returning a pointer for one off the wire.
- `rows_in_enum_order<&row_t::key>(table)` — the order check, for a
  `static_assert`.

`def_gen` emits `enum_traits<E>` at global scope after each generated
namespace closes, for every enum in both `.def` families. Entity enums also
carry `type` (the `enum_type` reflection id), which is the first compile-time
link between a C++ enum type and its runtime tag — that was previously written
by eye with nothing checking it. Cvar enums get `count` alone; that family has
no reflection enum. `SCHEMA_HASH` is computed from parsed `.def` content, not
emitted text, so none of this touched the wire handshake.

Converted: `WEAPON_DEFINITIONS` (`shared/weapons.hpp`) and `WEAPON_FIRE_SOUNDS`
(`client/weapon_fire_audio.cpp`), whose hand-rolled `fire_sound_for` range check
became `try_get`. New test `array_test` (22nd in `GAME_TESTS`, links nothing —
almost all of it is `static_assert`).

## Why no POD/class split

Considered and rejected. That distinction earns its keep in a DYNAMIC array,
where growth forces a `memcpy`-vs-move choice. A fixed-size array never
reallocates, so there is nothing to specialize on: a bare aggregate is trivially
copyable exactly when `T` is, and the compiler makes the call. It is also what
lets one sit inside an entity struct without breaking the blittable /
memcmp-diffable contract — a variant with hand-written copy semantics would be
the one shape that quietly breaks `capture_field_changes`.

## The part most likely to matter again

**`Enum_Array` fixes the LENGTH of the storage, not that you filled it.** A
short initializer value-initializes the tail, like any aggregate. So adding a
value to a `.def` enum does not fail the build at every table over it — the
table grows a zeroed row, which is precisely the silence the old
`std::to_array` "deduce the size from the rows, then compare against `_COUNT`"
spelling existed to prevent.

That is why A survives. `rows_in_enum_order` catches BOTH failures, because a
zeroed tail row reads as enum value 0 and so is not at its own index:

- reorder — swap two rows, every lookup returns a neighbour's data;
- short list — the new enum value arrives as a zeroed row.

So an enum-indexed table of hand-written DATA is not finished without the
`static_assert`, and its row type needs a member naming its own enum value in
order to have one. Runtime STORAGE (caches, handle arrays) has no key member,
wants the zero-fill, and needs none of this. `array_test` pins the short-list
behaviour so it stays a known property rather than a rediscovery.

Verified the way the 2026-08-05 hand-rolled version was: swapped two rows in
`WEAPON_DEFINITIONS` and watched the build fail with the assert's own message.

## Two things in item B's write-up that did not survive contact

- **B's third conversion target does not exist.** `trigger_action_list.hpp` and
  its X-macro were deleted at some earlier point. `Trigger_Action` in
  `entities.def` is now the only list and `fire_trigger_action`'s exhaustive
  switch is the obligation; the stale "Values mirror TRIGGER_ACTION_LIST"
  comment in the `.def` is fixed.
- **`find` is spelled `try_get`.** A nullable-pointer return is a failure
  channel, and the `try_` convention is total.

## Follow-up: the aim poses (same day)

`aim_pose_clips_t` was the first real conversion outside the `Weapon` tables,
and the first HAND-WRITTEN enum to specialize `enum_traits`. `animation.hpp`
closes `namespace assets` around the one specialization and reopens it, since
`enum_traits` is at global scope.

- `aim_pose_clips_t` is now a **type alias** for
  `Enum_Array<aim_pose_t, const animation_clip_t*>`. That is all it ever was: an
  array plus the two enum-indexing operators.
- `aim_poses_blend_weights_t::weights` and `aim_pose_set_t::poses` became
  `Enum_Array`s, so every `weights[(uint32_t)aim_pose_t::Forward]` lost its cast.
- `AIM_POSE_PATHS` in both `model_format_test` and `hitbox_rig_test` is keyed by
  the pose, which ties the fixture list to the enum's count.
- **`AIM_POSE_PREFIXES` is gone.** It was a second table of "forward", "upward",
  … carrying a comment asking it not to drift from the enum; `to_string` already
  returns exactly those strings, so `load_aim_pose_set` builds the filename from
  it and there is one list again.

**`assets::aim_pose_t` is gone; the enum is `entities::Aim_Pose` in
`entities.def`.** The intermediate step kept the hand-written enum and its
`Count` sentinel, on the reasoning that a hand-written enum has no other source
for its size — drop the sentinel and `AIM_POSE_COUNT` becomes a hand-maintained
`5`, and since it sizes every `Enum_Array` over the enum, a sixth pose arrives as
a zeroed row. Moving the declaration into the `.def` dissolves that instead of
trading against it: the generator emits `Aim_Pose_COUNT`, the `enum_traits`
specialization, `to_string` and `try_from_string`, so both the sentinel and the
hand-written `to_string` switch in `animation.cpp` were deleted.

Worth knowing about this move:

- **No entity field has this type and no id rides the wire.** It is in the `.def`
  purely for what the generator emits around an enum. That is a use of the file
  the existing entries did not have — every other enum there replaced a string or
  int on an entity field — so it is a precedent, not an instance of one.
- **It changes `SCHEMA_HASH`**, so builds either side of it refuse each other at
  connect. Bounded and deliberate (an enum only changes when someone edits the
  `.def`), unlike the scanned-directory manifest problem that keeps sounds out of
  the hash — see todo.md item C.
- **`entities::to_string` returns the `.def` spelling, `"Forward"`, but the
  exporter writes `forward_holding_gun.animation`.** So `load_aim_pose_set` now
  lowercases it in `filename_prefix_of`. That is a DERIVATION where
  `AIM_POSE_PREFIXES` was a second list; a transformation cannot drift. No test
  covers `load_aim_pose_set` (it is client-side), so the five derived paths were
  checked against the files on disk by hand.
- `animation.hpp` now includes `entities/generated/entities_generated.hpp`. No
  cycle: the generated header pulls in `array.hpp`, `linalg.hpp`, `span.hpp` and
  `network_types.hpp`, none of which reach animation.
- The unqualified `to_string(pose)` calls in `animation_tool.cpp` kept working
  untouched — ADL follows the argument type from `assets` to `entities`.

**This is what made `Array`/`Enum_Array` default to zeroed.** `aim_pose_clips_t`
was a struct with `= {}` on its member, so `aim_pose_clips_t clips;` gave five
nulls, and `sample_aim_pose`'s missing-extreme path depends on that. A bare
aggregate would have made those four call sites indeterminate. Both house types
now carry a `= {}` default member initializer: still aggregates, still trivially
copyable, and an explicit initializer replaces it so a constexpr table pays
nothing.

## Scope deliberately not taken

Runtime-storage sites — `mesh_handles` / `sprite_handles` (`shared/asset.hpp`),
the reflection caches in `entities/entity_reflection.cpp`,
`Entity_System::pools` — were left as raw arrays sized by `_COUNT`. Converting
them is churn: they are indexed by keys the code produced, they want zero-fill,
and there is no key column to check. Existing `std::array` uses that are not
enum-indexed (`snapshot_history`, the input tables, `player_slots`) were left
alone too; prefer the house types in new code rather than sweeping old ones.

---

# SKELETAL HITBOXES IN GAMEPLAY  ✅ DONE (2026-08-11)

Animation build-order step 4. The volumes had existed and posed correctly since
2026-08-10, and nothing shot at them: `hitscan.cpp` still resolved regions
against `player_hitboxes`'s three axis-aligned boxes. So the model leaned, an arm
swung out, and the bullet went through a column.

## `resolve_hitscan` takes VOLUMES, not a position

`hitscan_target_t` was `{uid, position}` and the function walked a static table
of three boxes. It is now `{uid, Span<const posed_hitbox_t>}` in world space,
and the function only ranks — every shape's geometry lives in
`intersect_ray_hitbox` (`hitbox_rig.cpp`), beside `distance_outside_hitbox`,
which is where that header always said the hit test would go.

Passing placed volumes rather than pose inputs is what keeps lag compensation
from needing a second entry point: §4 stores OUTPUTS in `Snapshot_History`
precisely so nothing is re-derived at rewind, and those arrive as exactly this
span.

## `shared/player_rig.{hpp,cpp}` is the new join

Three things existed separately — the aim blend, the bone→volume mapping, and a
player's gameplay state. `compute_player_hitboxes(rig, pose, settings, out)`
joins them: sample the five-pose blend, walk the hierarchy, place the volumes,
rotate by `body_yaw` and translate to the feet. The server calls it per target
per shot; the client's `debug_show_hitboxes` overlay calls it to draw. **One
function, so the overlay cannot show a volume the server is not testing** —
guarantee 1 of §4, bought the way that section says to buy it.

The world transform matches the RENDERER's (`Ry` sweeping +X toward −Z), not
`direction_from_angles`' (+X toward +Z). The drawn mesh decides where a limb
looks like it is; if the model faces the wrong way (`todo.md`, "Does the model
face the right way?") both move together and the fix stays in the exporter.

The rig loads eagerly in `server::init` rather than on first use. Loading it is
fatal-on-failure, and dying at boot naming the file beats dying inside the fire
path of a live match.

## `body_yaw` came forward out of step 5

Not optional: posing the volumes needs the torso twist, and the twist was a
client-local integrator with no server-side value to read. Every client held its
own copy and the server held none. So `body_yaw: f32 @Networked` on
`Player_Entity`, advanced once per fixed tick by the server, read (and
short-way-round interpolated) by clients.

Two details worth keeping:

- **The tick pass is over the ENTITY POOL, not the move inbox.** A bot sends no
  moves. Hanging the update off the move loop would leave every bot drawn and
  hit-tested permanently untwisted.
- **`cl_aim_max_pitch` / `_max_yaw` / `_body_turn_rate` became `sv_aim_*`
  `@Mirrored`.** They stopped being presentation the moment a hit decision read
  them: two sides disagreeing about `sv_aim_max_yaw` is two sides disagreeing
  about where a twisted torso is. Same argument as `pm_*`.

## Ray casting: four shapes out of three surfaces

`intersect_ray_hitbox` composes a sphere, a cylinder SIDE (the open tube) and a
disc. A capsule is the tube plus two spheres; a cylinder is the tube plus two
discs; a box is a slab test in its own frame. Writing the surfaces rather than
the solids is why the capsule is not a fourth copy of the quadratic.

**Every surface reports the ENTRY point and rejects a negative distance**, so "a
muzzle already inside a volume misses" holds uniformly instead of in three
shapes out of four. The one that needed thought: a ray fired from inside a
cylinder straight down its own axis never meets the tube, so the disc test had
to become front-facing-only or it would happily report the far cap from behind.
`hitscan_test` has that case; it caught it.

## What went away

- `player_hitboxes.hpp` → `hit_region.hpp`. The three static boxes are deleted;
  `hit_region_t` is all that was worth keeping. Ten-plus volumes over three
  regions is the point — the old comment conflated the two counts because with
  three boxes they happened to be equal.
- The Animation tool's "Static hitboxes" audit toggle, and its private copies of
  the wireframe helpers. Those moved to `client/hitbox_debug_draw.hpp`,
  templated on a line sink so the tool (`overlay_renderer_t`) and the game
  (`renderer::draw_line`) share one implementation without sharing a base class.
- `client/player_animator.{hpp,cpp}` → `shared/`. Both sides run the aim blend
  now, which is the whole reason the pose the client draws is the pose the
  server tests.

`Span<T>` gained a converting constructor to `Span<const T>` — a real hole, hit
the first time a caller held the writable span and needed the read-only one.

## What is left, and it is the honest remainder

**Lag compensation.** The server tests where the target is NOW; a shooter on
80 ms aimed at where it was. That is guarantee 2 of §4 (`Snapshot_History`
carrying endpoints per tick), it is the same machinery as replicating
`locomotion_phase`, and the two land together at step 5.

---

# Closed decisions — recorded so they are not re-litigated

### Why the five entity-track topics were one track

They shared four files and each phase changed the ground under the next:

```
                    pascal_string set() bug          [P0] DONE
                            |
                            v  (memcmp == string equality; both undo + wire rely on it)
  geometry exit  ---------> P1  DONE ---------------------------------------.
   . killed is_collision_geometry() routing           (map I/O rewritten)   |
   . killed the map<->session shared_ptr aliasing on the static path        |
   . means the DSL never needs [N]T                                         |
   . added the 2nd transaction flavor (geometry value-swap)                 |
                            |                                               |
                            v                                               v
  undo -> binary diffs ---> P2  DONE                  generator finish -> P3 DONE
   . transaction_system touched ONCE for both flavors                       |
   . removed get_all_properties/init_from_map from the hot path             v
   . shrank P5's dynamic-dispatch surface                       flag audit -> P4 DONE
                            |                                               |
                            '------------------> P5 HARD CUTOVER <----------'
                                 DONE
                                 . macro system deleted, network::Entity died
                                 . map save/load moved to generated tables
                                 . undo's text adapter landed here (disk boundary)
                                                  |
                                    .-------------+--------------.
                                    v                            v
                        serializer v2 -> P6              storage refactor -> P7
                        (+ snapshot delta compression)   (pools, handles, id unify)
                                    '-------------+--------------'
                                                  v
                                        protobuf removal -> P8
```

| Topic | Touched in |
|---|---|
| Entity generation | P3, P4, P5 (P1 shrank its input from 12 types to 8) |
| Schema stuff | P1 (geometry stops paying it), P2 (undo stands on it), P5 (deleted) |
| Entity spawning / ownership | P3 (factory helpers), P5 (virtuals die), P7 (pools + handles) |
| Map serialization | P1 (geometry I/O + map conversion), P5 (generated, declaration-ordered) |
| Undo / redo | P1 (2nd flavor), P2 (binary diffs), P5 (re-point + text adapter) |

**Ordering rules that were satisfied** (the P7/P8 ones still live in `todo.md`):

- **Geometry exits before the generator is wired in.** SATISFIED by P1.
  `Displacement_Entity`'s `schema_array_t<float32, 3267>` was the only
  array-typed field on any entity, and it's gone — so the DSL never needs
  `[N]T`. Do not add it.
- **P4 (flag audit) is blocking.** SATISFIED — see P4 above.
- **No compatibility phase anywhere in P5.** No `Class_Schema` shim, no
  per-entity migration. The generator emitted the end state; the cutover was one
  hard break and the tree didn't build until the last consumer was converted.
  That was the point — the compiler is the migration checklist.
- **COMMIT BEFORE P5.** Done on its own branch off a clean, green tree, so
  "is this broken because of the change, or was it already broken?" had an
  answer.

### API style: the house `Span<T>`

Settled 2026-07-27 (P3). `src/shared/span.hpp` is the one type for "a contiguous
range of T": the five generated pointer+count signatures were converted, and so
were the three pre-existing `std::span` sites (`input.hpp`, `cvar.hpp` and its
callers), so the codebase has ONE spelling rather than three. The "generated
output depends on nothing but `<cstdint>`" argument for a house type was already
spent — the generated header includes `linalg.hpp`, which pulls `<algorithm>`
and `<cmath>`. It earns its place on consistency and on not making every entity
TU pay for `<ranges>`.

`Span<T>` deliberately has no `Array<T>` sibling for FIXED-size arrays. Nothing
needs one; if something does, it is a different type with a different name, not
an overload of this one.

### Generated output file layout — DEFERRED (considered 2026-07-26)

**One file per entity is the WRONG cut.** Four reasons, and the first is fatal:

1. **There is no incremental-rebuild win to have.** `CMakeLists.txt:126-131` is
   `DEPENDS entity_gen ${ENTITY_DEF_FILE}` — the whole `.def` plus the generator
   binary. The unit of change is the `.def`, not the entity, so editing one field
   on `Light_Entity` regenerates everything no matter how many files that is.
   Per-entity files add file count without adding rebuild granularity. (Even with
   content-compare writes that skip untouched files, the tables in point 2 change
   on every edit anyway.)
2. **Half the output cannot be split.** `entity_type`, `ENTITY_INFOS[]`,
   `COMPONENT_OFFSETS[][]`, `entity_type_from_classname` and `SCHEMA_HASH` are
   whole-program tables spanning every entity. The result is N+1 files where the
   +1 churns on every change — the churn isn't removed, just surrounded.
3. **Declaration order becomes the generator's problem.** Emission order makes
   correctness free today: enums, then components, then the base, then the
   entities embedding them. Split across files and the generator must emit
   correct includes and topologically sort what it currently just writes in
   sequence. Real complexity, no payoff.
4. **It scatters the diff the layout exists to produce.** These land in the
   source tree specifically so a `.def` change reads as one reviewable diff
   (`CMakeLists.txt:115-119`). Per-entity files fragment one logical change
   across N files. The navigation argument cuts the same way — the file you
   actually read is `entities.def`; the generated header says "Do not edit."

**The split that WOULD be right, on a different axis: by ROLE, not by entity.**
Structs + enums in one header (every consumer needs those) and the reflection
tables in another (only the serializers, the editor inspector, and undo touch
those). Today every TU including the generated header pays for tables most of
them never read.

**Trigger to revisit** — needs BOTH, not either: entity count grows severalfold
(8 today; the tables are ~340 lines), AND the generated header shows up in an
actual compile-time profile. Until both hold this is speculation. Note P5
weakens the case further: once the tables drive map I/O, undo and networking,
most entity-touching code wants them anyway, so the "who needs what" boundary
gets blurrier, not sharper.

### Sprite asset placeholder

- [x] `sprite_asset` has no `placeholder` (there is no `error.png`), so its slot
      0 is `ASSET_SOURCE_MISSING` with an empty source. `assets::init()` now
      `log_error`s when it meets one rather than skipping it quietly
      (`asset.cpp:1062-1068`) — it fires on every launch until a sprite
      placeholder exists, which is the point.

### Default mesh is the question mark

`mesh_asset::Missing` is id 0 and resolves to `resources/obj/error.obj`, so an
unassigned mesh field is the question mark by construction (P3). The runtime
half — `assets::init()` honoring that entry via eager registration — landed in
P5.

### Tests that used to fail

- ~~`network_test` SEGFAULTS~~ **FIXED in P5.** The fault was in the TEST, not
  the engine: it called `network::diff(nullptr, &entity, schema)` for its
  full-update case, and `diff()` memcmp'd the baseline unconditionally, so a
  null baseline was a null dereference. `Entity::serialize` had its own
  null-baseline branch and nothing else called `diff()` that way, which is why
  only the test ever hit it. Rewritten against the real serialize/deserialize
  path; passes.
- ~~`asset_test` fails (exit 3)~~ **FIXED.** Both faults were in the test:
  hardcoded POSIX `/tmp` fixture paths (so the `std::ofstream` wrote nowhere on
  Windows, silently, and the load then had no file), and an `assert(channels ==
  3)` against a loader that deliberately forces RGBA. Now uses
  `std::filesystem::temp_directory_path()` and checks the stream state.

---

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