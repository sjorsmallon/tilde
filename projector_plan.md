# Projector: solid shadows, a solid mask, and a picture that is a simulation

Three ideas that half overlap, and one spotlight carries all of them.

1. **Solid shadows.** An object standing in the light throws a shadow volume,
   and that volume is collision: a player walks onto a crate's shadow and
   floats, their own shadow standing on the crate's shadow in the picture.
2. **Solid mask.** The projector throws a picture, and the BLACK of the picture
   is collision: the pyramid from the lens through a black region to the wall.
   A player whose shadow rests on black is at rest. A ball thrown into the dark
   rests too, since everything swept meets the same piece.
3. **A simulated picture.** The picture is the output of a simulation that runs
   in the image plane, and the room enters it as silhouettes: the player's
   shadow is a wall in that world, so a projected ball bounces off them.

1 and 2 are one primitive, **dark is solid**: a convex pyramid in the predicted
world, its source an object in the room or a mask on the glass. 3 is the other
direction: the room projected INTO the picture, and the simulation is new code.
The server has no GPU. The depth map of the familiar sketch is the client's
shadow map, which exists; the server's version of it is the shadow volumes (1,
2) and the silhouette polygons (3).

## Status

- **§1 landed 2026-11-02**: `Light::shadows_are_solid`, `solid_shadows.cpp`, `solid_shadow_test`; the
  three light types are `@predicted`. 1b (players as casters) is open.
- **§2b landed 2026-11-02**: `Projector_Entity`, `light_kind_t::Projector`, `projector_shadow_projection`,
  the frame planes in `shadow_light_t`, `LIGHT_TYPE_PROJECTOR` and `projector_mask`, the picture array at
  binding 17 (`r_projector_picture_size`), the editor row and gizmo, `projector_test`. A projector's `mode`
  is folded as Dynamic whatever it says and `validate_map_paths` names a map that says otherwise. Not yet
  looked at in the game: the first projector level is the gobo plate of §2a.
- **§2c landed 2026-11-03**: `projector_mask.{hpp,cpp}`, `projector_mask_test`. Simpler than written below:
  the picture is read on a grid of at most `PROJECTOR_MASK_GRID_LIMIT` (64) cells a side and its dark cells
  (mean of r, g, b under `PROJECTOR_MASK_DARK_THRESHOLD`) are covered by greedy RECTANGLES, not marching squares
  and a polygon decomposition: a rectangle is already convex and abuts its neighbour, so the texel-aligned
  cover IS the decomposition. Each rectangle is a slab of the frame's pyramid at the glass
  (`projector_mask_glass_distance`: where the frame is `PROJECTOR_MASK_GLASS_HALF_HEIGHT` tall, so a cell is
  units wide whatever the angle) cast through `compute_shadow_volumes` as a piece whose owner is the projector,
  into that projector alone. The dark between the lens and the glass is not solid. The mask is cached per
  texture in `game_session_t::projector_mask_cache`, computed on the first tick that asks; `shadow_light_t`
  carries the picture and the frame's unit corners. Capped at `MAX_PROJECTOR_MASK_RECTANGLES` (48), logged.
  The shadow map draws nothing new: the picture is already dark there (`projector_mask` in the shader), and
  the rectangles agree with it to the grid cell.
- Next: 1b (players as casters), then §3a. Still not looked at in the game: a projector level.

## Non-goals

- **The lit part solid.** `solid_beam` is that already; this record is about
  the dark, and nothing here carves a lit pyramid.
- **Pixels as collision.** A texture becomes collision only through polygons
  and a convex decomposition (§2c), capped; video stays visual.
- **Baked or Mixed projectors.** The projector is Dynamic: the mask changes, the
  bake skips Dynamic lights already, and Mixed would spend a bake slot for a
  bounce that is the picture's average.
- **A pocket map, a mini scene rendered to the picture, 2D physics.** None
  exist. The simulation of §3 is entities of the main map stepped by a system
  and drawn by the shader from their state; a render-to-texture pass is not
  needed until a picture wants more than shapes.
- **The picture in the beam's air.** A projector has no `beam`.

## 1. Solid shadows — `Light::shadows_are_solid`, shadow_volume.cpp, predicted_world.cpp

`Light` gains `shadows_are_solid: bool = false @Networked @Editable`. A volume
thrown by such a light is appended to the predicted world's movers as a
`mover_t` with equal poses, `crushes = false`, one `collision_piece_t` of the
volume's planes: `compute_solid_shadows(Span<const shadow_volume_t>, const
Entity_System&, std::vector<mover_t>& out)` after `compute_shadow_volumes`,
where `compute_solid_beams` sits. A volume is already a convex set of planes
cut to what it lands on, so the piece is the volume and nothing is built.

- **Casters.** Whatever casts today: geometry with `casts_shadow_volume`,
  movers carrying it. Players cast nothing today; a player becoming a caster
  into solid-shadow lights is §1b.
- **Slope.** Standing on a volume's top face is 3D collision, and that face's
  slope is the lens height: a lens at ledge height throws a flat face, a high
  one a ramp, and past 45 degrees the kernel's ground test (`normal.y` against
  `GROUND_NORMAL_MINIMUM_Y`, in seven places of movement_kernel.cpp) makes it a
  wall or a surf ramp per model. NOT SUPPORTED today: a piece that is ground at
  any slope, which is what "the shadow rests, so I rest" literally asks. It
  would be one `hit_is_ground(hit)` helper replacing the seven comparisons,
  then a flag on the piece the helper reads. Deferred until the gobo level
  shows the slope is a nuisance rather than a mechanic.
- **Width.** The drawn shadow is the skeletal mesh; the sweep is the hull,
  which contains it. When the shadow's feet visibly touch the dark edge the
  hull is already held, a few units high: the normal ledge, where the visible
  toes hang past the edge.
- Test: `solid_shadow_test`, a box caster, a light with the flag, a hull on the
  volume's top face is grounded and off the volume is not.

### 1b. Players as casters

`compute_player_shadow_volumes`: the hull as a caster piece into every
switched-on light with `shadows_are_solid`, so other players stand on your
shadow. A player is EXCLUDED from their own volume's piece (`mover_t` gains the
uid it holds up nobody of), or they stand on their own shadow forever. The
cost is one volume per player per such light; the scene block's drawn volumes
have a cap, and a volume past it is solid and undrawn, logged.

## 2. Solid mask

### 2a. The gobo — nothing new

A black region of the picture is a caster painted on the projector's glass. A
plate with holes placed before a spot with `shadows_are_solid` IS that, built
from map geometry in the editor today, drawn by the shadow map and solid by §1.
The smallest playable version of 2, and the test level for §1.

### 2b. `Projector_Entity`, `light_kind_t::Projector` — lighting.{hpp,cpp}, renderer.cpp, direct_light.glsl

The rectangular light the picture needs: aspect, roll, a texture, hard edges.

```
Projector_Entity :: entity @predicted is Colorable, Switchable {
    switch_state:       Enabled
    light:              Light
    rides:              Rides
    range:              f32 = 512.0    @Editable
    // Half the VERTICAL angle of the frame, from the axis; the width is aspect times the height.
    half_angle_degrees: f32 = 20.0     @Editable
    aspect:             f32 = 1.7778   @Editable
    // What the frame shows. Missing is no picture: a plain white frame.
    texture:            texture_asset = .Missing @Editable
}
```

A new entity, not a spot option: the cone, the inner/outer falloff and the beam
are what a projector lacks, and aspect and roll are what a spot lacks. Roll is
free, the base `orientation` is a quaternion and `basis_from(orientation).up`
is the frame's up. `Light` stays whole, so `shadows_are_solid`, `source_radius`
and `cuts_geometry` mean what they mean on every light; `mode` is read and
refused (non-goals).

- `light_kind_t` gains `Projector`; `scene_light_t` gains `up` and `aspect`.
  `try_convert_light_entity_to_scene_light` gets its fourth arm. `shadow_volume.cpp`
  casts per kind through `entity_as`; the projector arm clips its volumes to
  the frame's four planes where a spot's are clipped to `planes_of_reveal_cone`.
- `light_arrival.glsl` gains `LIGHT_TYPE_PROJECTOR`: a spot's arrival without
  the cone factor. The frame is the cutoff and falls out of the sampler below.
- **The frame is the shadow map.** `projector_shadow_projection(light,
  resolution)`: `perspective(2 * half_angle, aspect, SHADOW_NEAR_PLANE, range)`
  times `look_at(position, position + forward, up)`. A projector always takes
  a shadow layer, `casts_shadows` or not, because that layer's
  `shadow_view_projection` is also the picture's uv: one matrix per projector
  and no second array. Past the pool it draws no picture, logged once.
- **The picture array is indexed by shadow layer.** `layout(set = 3, binding
  = 17) uniform sampler2DArray projectorPictures`, `MAX_SHADOW_LAYERS` layers
  at `r_projector_picture_size`, clamp-to-border black. A projector's texture
  is uploaded into the layer it was assigned; the `Light` struct, whose four
  `.w`s are taken, does not grow. Spots never sample it.
- `direct_light.glsl`: `vec3 projector_mask(Light light, vec3 world_position)`
  samples the picture at the layer's uv (the `clip` of `sample_shadow_layer`
  without the receiver offsets); black outside the frame by the border, the
  hard rectangular edge. `gather_direct_light` multiplies it into `visibility`
  for a projector, the vec3 the atlas coverage already made that argument.
- Editor row in `EDITOR_DATA_PER_ENTITY_TYPE`; the gizmo is the spot's cone
  drawing with four edges.
- Test: `projector_test` maps the frame's corners at `range` through the
  projection and asserts uv {0,1}² and ndc.z 1.

### 2c. The mask from the texture — shared/lighting/projector_mask.{hpp,cpp}

`compute_projector_mask_from_picture(picture, texture, threshold)`: the dark of
the picture as rectangles of a grid (see Status for what landed), each a caster
piece on the glass, fed to `compute_shadow_volumes` as any caster, so the volume
is cut to the frame and the range and §1 makes it solid. Computed once per
texture per session, shared by both ends (the texture is an asset, hashed in
the handshake). The drawn dark is the texture itself through `projector_mask`;
the solid dark is the same bytes under the same threshold, to the grid cell.

A video frame does not go through this; a moving mask is §3's shapes.

## 3. The simulated picture

The room enters the picture (3a), the picture has state that moves (3b), and
the picture acts on the room (3c, 3d). The shapes are drawn by the shader from
the scene block, and the dark ones are solid through §1 by being casters on the
glass exactly as §2c's parts are: a shape is a mask that moves.

### 3a. Silhouettes: the room into the picture — shared/lighting/projector_silhouettes.{hpp,cpp}

`compute_projector_silhouettes_at_tick(session, projector, tick, out)`: for a
player whose hull meets the frustum, the corners of `compute_player_hitboxes`
projected through the frame and reduced by `convex_hull_2d_from_points`; for a
caster piece that throws into this light, its corners the same way. Convex uv
polygons per projector: what the simulation bounces off.

Hitboxes, not the hull: a blocker wants the tight shape or the ball bounces
off empty air beside the visible shadow. Standing (§1) wants the generous shape
and gets it from the sweep. Casters from the start, or a ball passing through
a crate's shadow while the player's stops it reads as a bug.

### 3b. `Projected_Ball_Entity` — shared/lighting/projected_ball.{hpp,cpp}, server/systems/projected_balls.cpp

```
Projected_Ball_Entity :: entity @predicted is Colorable {
    projector:       entity     @Networked @Editable
    radius:          f32 = 0.03 @Networked @Editable
    launch_tick:     u32        @Networked
    launch_uv:       v3_exact   @Networked
    launch_velocity: v3_exact   @Networked
}
```

A ball between bounces is a parabola, so it is LAUNCH state read by
`projected_ball_uv_at_tick(ball, tick, tickrate, gravity)` on both ends, like a
platform or a bubble, never a per-tick networked position. Gravity is
`mp_projected_gravity` in frame heights per second squared along -v; the roll
decides what that is in the room. `update_projected_balls` sits in the tick's
"rest of the world" step: evaluate, test the disc against the frame's edges and
3a's polygons, on a hit write a new launch. The server's outline of your own
shadow is half a round trip behind your predicted body; the step is pure in
its inputs and can be predicted when that matters.

Drawn as a disc in `projected_shapes[MAX_PROJECTED_SHAPES]` (scene block, two
vec4 per shape: extent, and shadow layer, kind, colour), evaluated at the drawn
time by the same `_at_tick`; `projector_mask` returns the shape's colour over
the picture. `try_compute_drawn_entity_at_tick` gets the case, saying this type
has no mesh. A dark ball is also a caster on the glass, so a player stands on
it while it rests (§1) and it is pushed by nothing, since nothing simulated
rides.

### 3c. The ball in the room — server/systems/trigger_system.cpp

Where the ball is in the room is where its picture is: `projector_ray_hit`, the
ray from the lens through its uv against `session.bvh`. That point is a toucher
in the trigger loop with the ball as the touching entity, so a ball landing on a
`Trigger_Volume_Entity` emits `Touched` and the connections press the button;
the same point places the sound. A point, not the pyramid, because "falls on
the button" is about the picture on the surface and a pyramid reaches through
doorways.

### 3d. Handoff

A ball leaving the frame through its bottom edge spawns a bounce body at its
`projector_ray_hit`, `launch_velocity` mapped through the frame basis, and is
destroyed. The reverse, a bounce body entering the frustum captured into a
ball, is the mapping inverted behind a flag on the projector, when a level asks.

## 4. Video, separately

A decoder is the whole problem; the engine's side is one upload per frame into
the picture layer. A single-header decoder under `third_party/` (pl_mpeg for
MPEG-1, or raw frames for a first test), a new asset extension with its
`decode_<ext>`, a clock on the projector. Visual only: a frame is not a mask.

## Order

1 (solid shadows, with a gobo plate as the level) → 2b (the projector light and
its white frame) → 2c (mask from a texture) → 1b (players as casters) → 3a →
3b → 3c → 3d → 4. The first two are the feature. Everything in 3 is new code
with nothing to lean on but the shadow machinery, and comes when a level wants
a game in the picture.

## Further ideas, not planned

- **Shadow touch.** A silhouette overlapping a shape emits `Touched` from the
  shape: "stand so your shadow covers the picture" as a switch.
- **Moving masks.** A mask part driven along a 2D path by a timer; a solid one
  carries the player, which a mover with unequal poses already does.
- **The 2D panel.** An ImGui view of a projector's frame with the shapes
  draggable, writing the same fields through the same transactions.
- **A monitor.** Rendering the room from the projector's camera into the
  picture is render to texture, a different feature.
