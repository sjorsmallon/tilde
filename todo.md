
- orientation is not clear whether it uses euler angles / degrees / radians. we are inconsistent. that's not good
- nested schemas are annoying. can we clean that code up?




- is the navmesh only planar? or does A* just need two-dimensional? I think there's something wrong.




- gizmo for selection moving is not finalized.
- undo/redo: multi-entity delete creates one transaction per entity. Ctrl+Z only undoes one deletion at a time instead of the whole batch.


Sprite transparency — smoke.png has opaque backgrounds that need 
alpha masking
Particle editor tool — dedicated ImGui panel for live parameter tweaking
Easing functions — replace linear lerp with ease-in/out curves





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
