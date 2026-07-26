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