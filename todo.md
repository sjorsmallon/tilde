- rocket launcher
- sculpting is wrong: after ininitial resize, the face is in the wrong way.
- undo/redo: entity_placement_t position/scale/rotation not tracked by transaction system. Gizmo translate/rotate syncs placement fields but undo only restores entity schema properties. After undo, placement and entity fields may be out of sync until BVH rebuild.
- undo/redo: multi-entity delete creates one transaction per entity. Ctrl+Z only undoes one deletion at a time instead of the whole batch.

## Multiplayer Networking (done: basic wiring)
- [x] fixed server tick loop (accumulator-based, sv_tickrate)
- [x] client connection handshake (CmdConnect/CmdAccept/CmdReject)
- [x] client sends C2S_PlayerMoveCommand to server each tick
- [x] server runs player_move() authoritatively on received input
- [x] server broadcasts S2C_EntityPackage snapshots to all clients
- [x] client receives and deserializes entity snapshots
- [x] client-side prediction with server reconciliation
- [x] remote player interpolation (2-snapshot buffer)

## Multiplayer Networking (TODO: next steps)
- delta compression for snapshots (currently full updates every tick)
- ack/nack system for reliable packet delivery
- heartbeat / keep-alive / timeout for stale connections
- configurable server address (currently hardcoded 127.0.0.1)
- player model rendering for remote players (currently wireframe AABB)
- lag compensation
- bandwidth throttling / send rate limiting
- replicated CVar sync from server to client

- why is AABB a schema? it's not a good decision.
- all components that exist now should define a schema. is that what we want?
- make sure the default mesh is the question mark.
- clean up BVH traversal because we now just iterate over entities in the map editor.
