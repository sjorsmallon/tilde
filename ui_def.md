# UI — Design

Outcome of the design discussion on 2026-08-17, and the shape slice 1 landed
that day. The goal, in the user's words: **nice font rendering, as lightweight
as possible**, and a HUD that binds to game state without a framework.

Before this there was no UI system at all — only ImGui. Every glyph on screen
came from its built-in ProggyClean 13px bitmap, and `draw_announcement` scaled it
2x, which is why it looked the way it did. There was also no screen-space draw
path of any kind: `todo.md` had wanted `draw_fullscreen_texture` for the scope
overlay since forever, and `assets::get_sprite` had zero consumers because a
sprite could not be drawn.

## The three decisions

### 1. ImGui keeps the tools. The game gets its own layer.

Two systems, on purpose, with the boundary drawn at **retained widget state and
text entry**:

| | ImGui | the client UI layer |
|---|---|---|
| Editor, console, debug panels | ✅ | |
| HUD, crosshair, announcements, overlays, **menus** | | ✅ |

> **Slice 1 drew this boundary at *interactivity* and put menus on ImGui's side.
> Slice 2 moved it**, because that line was wrong. Interactivity is cheap: a
> hit-test and a focus id. What ImGui is genuinely irreplaceable for is *scroll
> position, focus across many widgets, window management, and typing* — an
> editor's problems, none of which a vertical list of labels has.

That is also the honest reason the **console stays ImGui**: it is text entry, and
the client layer deliberately cannot do it. Free-form text would mean extending
`key_t` with punctuation and adding a keycode→character table (correct on US
layout only), or plumbing `SDL_TEXTINPUT` so the OS handles layouts, dead keys
and IME. Neither is worth it until something in-game needs typing.

**Consequence, accepted:** ImGui composites last, so the crosshair, the
announcement *and now the menu* draw *below* every ImGui window. An open console
covers the crosshair and the menu behind it — and `gather_ui_input` drops the
actions ImGui claims, so the menu does not react to keys being typed into it.

### 2. Immediate-mode rendering over a retained MODEL

The question was "how do UI elements bind to in-game values like health". The
three patterns in the wild:

- **push** — `hud->set_health(n)` from wherever health changes. Worst: N call
  sites, each forgettable, and the copy can disagree.
- **observer / dirty-bit binding** — the widget holds a getter and re-evaluates
  when something marks it dirty (UMG property bindings, web reactive frameworks).
  Correct when re-running layout is expensive.
- **immediate mode over a retained model** — rebuild the whole thing every frame
  from the authoritative state.

The third, and the reason is not taste. The HUD is ~20 elements; re-deriving all
of them at 144Hz costs microseconds, so the work that reactive bindings exist to
avoid is not work worth avoiding. What the other two buy in exchange is a second
copy of `health` that can be wrong — the same failure this codebase has already
paid for three times (`body_yaw` integrated locally, `held_snapshot_tick`
assigned rather than maxed, `last_broadcast_cvars` as a `Set()` you could
forget). There is no `hud_health`. There is `latest_player_entities[my_slot].health`,
read where it is drawn.

**Immediate mode is not an excuse to have no model.** Anything with a
*lifetime* — kill-feed rows fading out, a damage flash, a "+50" popup — is state,
because the event that creates it fires once, on a different clock than render
frames. That state belongs in a `hud_state_t` beside `visual_effects_t`, fed by
the event channel, retired per frame exactly like `debug_draw_list_t::retire(dt)`.
The draw stays a pure function of it.

#### Sharpened in slice 2: what "retained model" is allowed to hold

The retained UI screen (`ui/screen.hpp`) looks like it contradicts all of the
above. It does not, once the split is named: **structure is retained, values are
rewritten.** A property has exactly one of three owners.

| Kind | Owner | Example | Rule |
|---|---|---|---|
| **authored** | the build | row labels, the parent/child wiring, which nodes are focusable | written once, by the function that returns the screen |
| **bound** | a source outside the node | health, the join address, every rect, a row's tint | rewritten **every frame** from that source; never cached anywhere else |
| **animated** | the screen | opacity fade, highlight slide | advanced by `dt`; has a *lifetime*, so it is legitimately owned |

Structure is retained because focus and animation need identity across frames —
a function that re-derives its layout every frame gives a tween nothing to hold.

**"Bound" is about where the value comes from, not who it comes from.** Slice 2
defined it as "from the game" and drew the line in the wrong place: a rect comes
from the live screen size and a row's tint comes from the screen's own focus, and
both are sources exactly as the game is. The doc called `tint` authored while the
code rewrote it every frame — the code was right. Under the corrected definition
there are three sources, so the main menu has exactly two bound passes, cut by
which source they read:

- `advance_list_menu(menu, dt, screen_size)` — the tweens, then every rect (from
  the screen size), then every tint (from `focused_node`).
- `write_bound_values(menu, context)` — what is left, which is only ever what
  comes from the GAME: for the main menu, the join address out of
  `client_context_t`, through `write_list_menu_row_value`.

Both write unconditionally, which is what makes staleness **unrepresentable**
rather than merely discouraged: the write depends on nothing, so there is no flag
to miss and no subscription to leak. The old name for this was `refresh()`, which
named no source and covered one string while `layout()` quietly did the rest.

They run at the **end** of `update`, after input. Input then resolves against the
layout that was actually drawn — which is what a click on a row means — and a
focus change is on screen the same frame instead of the next one.

Animation is the exception that proves the rule: a fade is *not* a copy of game
state, it is state whose entire existence is a lifetime — the same category as
a kill-feed row.

**Why not signals/slots**, which is the obvious alternative: authoritative state
here arrives as *snapshots* — `latest_player_entities` is replaced wholesale — so
there is no "health changed" moment to emit. A signal system degenerates into
"emit everything every snapshot", which is polling plus a subscription table.
Dirty flags are not an alternative either; they are an optimization over push or
polling, and something still has to set the flag.

**The rule, in one line: continuous values are polled from the truth; discrete
occurrences are pushed into a model with a lifetime, and that model is polled.**

The one seam this leaves open is *structural*: rewriting values is enough only
while a screen's shape is fixed. A scoreboard whose row count varies needs the
subtree rebuilt when the count changes — deliberately not structural diffing,
which is the reactive machinery being avoided.

`hud/announcement.cpp` is the smallest complete instance of the pattern and the
one that shipped in slice 1: a string and a countdown, aged in the draw call, and
nothing else.

`announce(text: string...)` is a **command**, not a cvar, for the same reason
`map` is one: announcing is a verb, and the banner has no "current value"
anybody can ask for — three seconds later the honest answer is nothing. Its
handler lives in `hud/announcement.cpp` rather than beside `bind` and `connect`
in `console.cpp`, because those two are about the console itself and this one is
not; the handler sits next to the only two variables it touches. `string...`
takes the line's untokenized tail, so `announce round starts in 3` keeps its
spacing — which also means `hud::announce` takes a `std::string_view`, since
that tail is a view into the console buffer and is not null-terminated.

### 3. Not declarative, and specifically NOT a `.def` family

The instinct in this codebase is to reach for `def_gen`. It is wrong here, and
the test says why: **every `.def` family exists because two parties must agree on
a declaration** — client and server on entity layout, both processes on cvar ids,
a fire site and its handler on an event payload, disk and code on asset names.
A HUD has no second party. Generating it would buy no agreement and cost a
compiler.

A hot-reloaded layout *file* is the real escalation path, and `shared/file_watcher.hpp`
already exists for it. It is deliberately not slice 1: a layout format is a
commitment to a shape, and the HUD does not have a settled shape yet.

## What shipped

```
resources/fonts/Roboto-Medium.ttf      Apache 2.0, via ImGui's misc/fonts
src/shared/stb_truetype.h              vendored beside stb_image.h, v1.26

src/client/ui/font.{hpp,cpp}           bake, measure_text, draw_text   (GPU-free)
src/client/ui/layout.hpp               ui_rect_t, anchored(), inset()  (header-only)
src/client/ui_draw_list.cpp            batching + vertex emission      (GPU-free)
src/client/hud/announcement.{hpp,cpp}  the banner, off ImGui, + the console command
src/client/hud/crosshair.{hpp,cpp}     ported to ui_draw_list_t

renderer.hpp                           ui_vertex_t / ui_batch_t / ui_draw_list_t
                                       render_frame(passes, ui), screen_size()
resources/shaders/ui.{vert,frag}       one pipeline for every screen-space quad
src/test/ui_test.cpp                   the bake, batching, layout — no device
```

### Slice 2 — the retained tree, animation, navigation

```
src/client/ui/screen.{hpp,cpp}         ui_screen_t, resolve_node, draw_screen,
                                       animate(...).from().to()             (GPU-free)
src/client/ui/navigation.{hpp,cpp}     find_neighbour, hit_test             (GPU-free)
src/client/ui/ui_input.{hpp,cpp}       ui_input_t, gather_ui_input          (the SDL half)
src/client/ui/font.{hpp,cpp}           + text_align_t, draw_text_aligned
src/client/states/main_menu_state.*    off ImGui, onto the screen

renderer.{hpp,cpp}                     + logical_window_points_to_framebuffer_pixels()
```

Six decisions worth keeping:

- **The nodes, the tweens and the focus are ONE type.** They were three —
  `ui_tree_t`, a `ui_animator_t` beside it, and a focus id on the state — and all
  three are addressed by the same `ui_node_id_t`, which is meaningless except
  against the nodes it was minted from. Held apart, a rebuilt tree leaves every
  animation and the focus naming whatever node now sits at that index, silently,
  because an id is a `uint16_t`. The evidence was already in the menu: `on_enter`
  had to hand-clear the animator *and* hand-zero every node's `offset`, a manual
  fixup for an invariant the types did not carry. One type makes it a consequence
  instead of a rule, and `ui_test`'s
  `test_rebuilding_a_screen_replaces_its_animations_and_focus` is the guard.
- **A screen is built as a VALUE, once per visit.** `build_list_menu()` returns
  the nodes and the handles into them together and `on_enter` assigns the whole
  thing. What it replaced was a `build_tree()` method behind an
  `if (tree.empty())` sentinel — a lazy constructor, which is what made it read as
  arbitrary: nothing said when it could run or what it invalidated. Minting and
  consuming ids in one breath deletes the sentinel, the clear, the fixup loop and
  the entire "is this handle still good" question. It is eight nodes.
- **Focus is the node a *non-positional* activate would hit** — remembered,
  exactly one, and it survives the pointer leaving the window. **Hover is not
  stored**, because it is not state: a positional input resolves its own target
  with `hit_test()` at the moment it arrives, which is what a click must do
  anyway. Whether pointer *movement* also writes focus is a per-screen **policy**
  — the main menu says yes, so there is one highlight and one meaning for
  activate — and it is one commented line in `update`, not a fact about the type.
- **Nodes are addressed by id, never by pointer.** `animate(node.opacity)` is the
  obvious spelling and it binds to an *address*; `nodes` is a vector that
  reallocates and screens get rebuilt, so it dangles. A `(node id, property)`
  handle also survives being written to a file, which is what keeps a
  data-driven format reachable.
- **Navigation is geometric, not a linear index.** "Down" is the nearest
  focusable node actually below this one, scored by distance along the axis plus
  a penalty for lateral drift. The extra cost is one loop; what it buys is that a
  two-column screen or a grid navigates correctly the day it is built, instead of
  being the thing that forces a rewrite. `ui_test` covers the two-column case
  precisely because it is the one an index gets wrong.
- **`ui_input_t` is abstract actions, not keys** — navigate / activate / cancel.
  Nothing downstream knows which device drove it, so adding `SDL_GameController`
  touches `ui_input.cpp` and nothing else. It is also why `ui_input.cpp` is the
  one file in the layer *not* compiled into `ui_test`: everything else takes a
  `ui_input_t` by value, so the whole interaction model tests with no window.
- **`logical_window_points_to_framebuffer_pixels` exists because the mouse and the draw list are in
  different spaces.** `SDL_GetMouseState` reports logical window points;
  `ui_draw_list_t` and `screen_size()` are framebuffer pixels from
  `SDL_Vulkan_GetDrawableSize`. With `SDL_WINDOW_ALLOW_HIGHDPI` those differ on a
  scaled display, and a hit-test in the wrong one sits at a growing offset from
  the text it belongs to.

### Slice 3 — the second screen, and the one widget it justified

```
src/client/ui/list_menu.{hpp,cpp}      list_menu_t, list_menu_style_t     (GPU-free)
src/client/states/pause_menu.{hpp,cpp} the rows, and cancel == resume
src/client/states/play_state.*         the ImGui overlay deleted
src/client/states/main_menu_state.*    ported onto the widget
```

The pause menu was going to be a copy of the main menu: same build, same two
bound passes, same highlight tween, same nav/hover/activate step. What actually
differed was three things — where the block sits, what the rows say, and what
activating one does — so the first two became a `list_menu_style_t` plus a label
list and the third stayed at the call site. `list_menu.cpp` never learns what a
row means: `update_list_menu` returns a **row index**, and the screen casts it to
its own `..._item_t` and switches. That is what keeps "adding a row is a compile
error at the dispatch" true for both screens without the widget knowing either
enum exists.

Three things worth keeping:

- **The style's defaults ARE the main menu's numbers**, so that screen passes
  `list_menu_style_t{}` and the pause menu names exactly one member: the
  backdrop. A menu that restated the defaults would just be a second place to
  change them, and the two screens genuinely are the same block in two places.
- **A transparent backdrop means no backdrop NODE**, not an invisible one. The
  pause menu needs the game behind it dimmed; the main menu draws over an empty
  frame and should not pay a full-screen quad per frame to say so.
- **Cancel resolves to RESUME**, in `pause_menu.cpp` rather than in the widget.
  Backing out of a pause menu and choosing "Resume" are the same act, which is a
  fact about *this* screen — a confirmation dialog's cancel is not its first row.
  The keypress that OPENS the menu is handled in an else-branch in `play_state`
  for the mirror-image reason: while the menu is up it owns Escape, so opening it
  must not also reach it as `ui_input_t::cancel` on the same frame.

The pause menu draws from `build_frame` **ahead of** the `world.ready` bail: a
client still streaming a map has no session to draw and can still open the menu,
and "Disconnect" is the only way out of a stalled download. `draw_imgui_panels`'s bail
survives the port, because ImGui composites on top of the UI list — a debug panel
left up would sit over the menu instead of under it.

There is **no transform or matrix in the draw list**, and that is a decision, not
an omission: world-space UI (a readout on the weapon, a terminal in the level) is
off the table, so framebuffer pixels stay. Parent-relative rects already give
nested translation, which is all animation needs — animating the root's `offset`
slides an entire screen.

### The renderer knows QUADS, not fonts

`ui_draw_list_t` lives in `renderer.hpp` because `render_frame` consumes it —
same reason `debug_draw_list_t` does. But it has no idea what a glyph is: it
holds textured quads in framebuffer pixels, and `client/ui/font.hpp` sits one
layer up and *produces* quads into it. Glyph packing, metrics and text layout
never enter the renderer.

That seam is what makes the whole layer testable. `font.cpp` and
`ui_draw_list.cpp` are separate translation units with no Vulkan call between
them, and `ui_test` compiles them directly rather than linking `game_client` —
the trick `debug_draw_list_test` established.

### One pipeline, one multiply

`ui.frag` is `outColor = fragColor * texture(atlas, fragUV)`, with no branch, and
it is correct for both callers because of two decisions upstream:

- the font bake expands 8-bit coverage to **white-with-alpha** RGBA, so a glyph
  samples `(1,1,1,coverage)` and the vertex colour passes through;
- `ui_draw_list_t::rect` passes an **invalid** texture handle, and
  `resolve_albedo_set`'s existing fallback ladder resolves that to the internal
  1×1 white — so untextured quads are not a second code path.

Positions are framebuffer pixels with a top-left origin (what every call site
thinks in, and what ImGui uses); the only conversion is one multiply in
`ui.vert` against a two-float push constant. Vulkan's NDC y already points down,
so there is no flip to get wrong.

Depth test off, culling off, alpha blend on, recorded once after every view pass.
Vertices ride a per-frame-in-flight bump ring, the same discipline as the debug
vertex buffer — a separate buffer because the format differs (20 bytes vs 40).

### A closed set of font sizes

`font_size_t { small, medium, large }` at 18 / 28 / 48 px. A raster atlas is
sharp at the size it was baked at and soft between, so an API taking an arbitrary
float would mostly return bad-looking text. Three named sizes are a decision, and
the call site reads as one.

### Snapped quads, no oversampling

The two are one decision, and taking half of it is what made the first version
blurry. **Oversampling buys subpixel POSITIONING and pays in blur**: stb
prefilters the glyph and shifts every offset by −(oversample−1)/(2·oversample),
so at 2× every `x_offset` sits on a quarter pixel and the quad is drawn at half
its atlas rect's size — a 2:1 minification through a two-tap bilinear filter with
no mips. The original `draw_text` floored the *pen* and then added those quarter-
pixel offsets, so it snapped nothing: 18px and 28px were resampled on every
glyph, while 48px (oversample 1, integer offsets) was accidentally the only sharp
size.

A HUD's text sits still, so the trade goes the other way. Oversampling is **1×1**
and `draw_text` rounds each quad's top-left to a whole pixel, adding the
*unrounded* extent so the quad is exactly as many pixels as the rect is texels —
bilinear then degenerates to a 1:1 copy. The pen itself keeps fractional
advances; rounding it per glyph would bias every step the same way and push a
word's tail visibly right.

The day text slides or scrolls, the trade flips back: 2×1 horizontal
oversampling and no snap. `ui_test`'s `test_glyph_quads_land_on_whole_pixels`
guards the current half — both that quads are integral and that pixel span
equals texel span, because a snapped quad of the wrong size blurs just as badly.

Dropping to 1×1 also cuts the atlas area 4×; the three sizes land in one
1024×256.

The pack is still **one `stbtt_PackFontRanges` call per size**, which now looks
like it could be collapsed into one and must not be.
`stbtt_PackFontRangesGatherRects` latches `missing_glyph_added` across every
range in a call: the first codepoint the font lacks gets a real `.notdef` rect
and every one after it gets a zero-area rect, which `RenderIntoRects` skips —
leaving that glyph with a **zero advance**. Batched, a codepoint the face is
missing (the shipped one has no `_`) draws at `small` and collapses to nothing
at `medium` and `large`. `ui_test` names the codepoint on the way to the assert,
because "some glyph at some size is dead" is not a diagnosis.

**A zero-area UV rect means "no ink"**, and the bake establishes that rather than
passing the packer's rect through: stb still allocates a one-texel rect for a
space (the padding), so taking it at face value had `draw_text` emitting six
vertices per space for half a pixel of transparency. `stbtt_IsGlyphEmpty` is the
honest test and makes the rule downstream one compare.

### Raster, not SDF

SDF buys arbitrary scale and free outlines/glow, and costs a smoothstep and a
softer look at small sizes. Raster is sharper where a HUD actually lives and is
strictly simpler. The growth point is deliberate and cheap: an SDF variant is a
flag on the batch plus one branch in `ui.frag`, and nothing else in the layer
moves. The announcement's drop shadow — one offset copy at a third alpha — is
what an outline would otherwise have been needed for.

## Deferred, with the reason

- **The HUD proper.** `hud_state_t`, kill-feed rows, health / ammo / weapon.
  `hud/kill_feed.cpp` still only `log_terminal`s; it is the obvious first
  customer and everything it needs is already plumbed.
- **Round timer and scoreboard.** The phase now has its transport:
  `Round_Phase_Changed` (`events.def`) fires from `enter_phase()` and the client
  announces it in `game_events/round_phase_changed.cpp`. The payload carries
  `phase_end_tick`, so a countdown widget is a local subtraction against the tick
  the client already tracks from snapshots — no per-tick traffic, and no further
  wire work. A scoreboard still needs one: there is no score field anywhere in
  the codebase.
- **Data-driven screens and a screen state machine.** The natural next step after
  the retained screen, and premature today: there is *one* screen, so the
  transition model and the file format would both be designed against imagined
  requirements. **The trigger is two or three hand-built screens** (options, a
  confirm dialog) — then the shape is observed rather than guessed, and the
  binding table `{node id, property, getter}` that replaces a hand-written
  `write_bound_values` is the first honest step toward it.
- **Gamepad.** There is no controller code in `src/client` at all. Navigation is
  already shaped for one — directional resolution, abstract actions — so this is
  `SDL_GameController` plumbing inside `gather_ui_input` and nothing else.
- **Text wrapping.** `draw_text_aligned` does alignment; one call is still one
  line. It lands when a screen needs a paragraph, which a menu does not.
- **Kerning.** `stbtt_GetGlyphKernAdvance` plus keeping the TTF bytes alive. It
  is an addition to the bake, not a change to anything.
- **Non-ASCII.** A longer `stbtt_pack_range` and a wider index map. The 32..126
  range is a size decision, not a structural one.
- **A UI scale factor.** Every pixel figure in `crosshair.hpp` and the HUD is
  literal framebuffer pixels today. A `hud_scale` cvar means re-baking the atlas
  on resolution change — which the registration split already makes easy, since
  the bake has no GPU in it.
- **Clip rects.** Nothing scrolls yet. This is the growth point for a scoreboard,
  and it is a per-batch `VkRect2D` plus a `vkCmdSetScissor`.
- **The 4-channel atlas.** `texture_asset_t` is RGBA by construction (stb_image
  is forced to it), so a coverage atlas costs 4× what it holds — 2 MB instead of
  512 KB. Fixing that means an R8 path through `upload_texture`, which is a
  renderer change for one caller. Not yet worth it.
- **`upload_texture`'s hardcoded LINEAR/REPEAT sampler.** Correct for a glyph
  atlas by accident rather than by contract: padded glyphs never sample outside
  [0,1], so REPEAT never wraps. A second UI texture with different needs is when
  the sampler becomes a parameter.
