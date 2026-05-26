
- orientation is not clear whether it uses euler angles / degrees / radians. we are inconsistent. that's not good
- nested schemas are annoying. can we clean that code up?


- irradiance map
- environment lighting
- pack PBR textures into one RGB (gltf tdoes occlusion, roughness, metallic (ORM))
- pack normal maps: store xy in RG (reconstruct Z) and use BA for roughness / height.


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
- client-side dynamic-entity prediction. Today the dedicated/networked client's Jolt world contains only static map geometry (via `populate_static_physics_bodies`); rockets / physics cubes / remote players are interpolated from snapshots, not simulated locally. Cosmetic effects sidestep this by only casting against static geometry (`cast_sphere_static`), which is byte-identical on client and server. If we ever want projectile prediction (fake-fire a rocket immediately, reconcile on server confirm) or local cosmetic queries against moving bodies, we'd need to register dynamic bodies into the client's Jolt world and step it. Until then, server-side casts whose result rides in the effect payload is the right shape.

- why is AABB a schema? it's not a good decision.
- all components that exist now should define a schema. is that what we want?
- make sure the default mesh is the question mark.
- clean up BVH traversal because we now just iterate over entities in the map editor.


## Physics body / Jolt
- [x] physics_body_entity (schema + entity_list registration)
- [x] physics_body_system: spawn_physics_body (box/sphere) + update_physics_bodies (reads Jolt transforms back)
- [x] spawn_cube and spawn_sphere console commands (server-flagged)
- [x] step_physics() + update_physics_bodies() wired into server::Tick()
- [x] integrated-mode render path via `server_session` pointer on `client_context_t`
- [x] networked replication: slot=254 sentinel in serialize/deserialize, delta compression matches rocket pattern
- snapshot interpolation for physics bodies on networked clients (currently snaps to latest snapshot each tick — visible stutter at server tickrate). Pattern to copy: `Remote_Player_State` with `snapshots[2]` + lerp in update.
- capsule shape: physics_body_system rejects "capsule" today because `register_dynamic_capsule` doesn't exist in physics.cpp. Add it when needed (Jolt has `JPH::CapsuleShape`).
- destruction: nothing unregisters the Jolt body when a Physics_Body_Entity is destroyed. Currently boxes never get destroyed, but as soon as one does (rocket explosion, kill volume, etc.) the body will leak. Wire `unregister_physics_body` into wherever destruction happens.

## Cross-cutting issues surfaced while adding physics_body
- type mismatch: `register_dynamic_box(physics, uid, ...)` takes `entity_uid_t` (uint32, defined in `map.hpp`), but `Entity::entity_id` is uint64. The maps in `physics_state_t` are uint32-keyed. Currently safe in practice because `next_entity_id` starts at 1 and increments, but it's a latent footgun — unify on one ID type for runtime-spawned entities.
- orientation precision: `Entity::orientation` is `vec3f` (euler), but Jolt internally uses quaternions. `update_physics_bodies` will need to convert quat→euler each tick, which is lossy and ugly near gimbal-lock. If/when spinning physics bodies look bad, add a `vec4f rotation_quat` schema field and replicate that instead.
- meson build (`meson.build`) is out of date: it's missing `static_entities.cpp`, `rocket_entity.cpp`, `particle_emitter_entity.cpp`, `displacement_entity.cpp`, `trigger_volume_entity.cpp`, `light_entity.cpp`, and now `physics_body_entity.cpp`. CMake is the primary build per CLAUDE.md, but either fix Meson to match or delete it.
