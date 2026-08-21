# Raw input timing — plan

Sub-tick edge times are currently **frame-quantized, not millisecond-accurate**, and
the code claims otherwise. This is the plan to fix it properly: take input
timestamps from a dedicated raw-input thread instead of from SDL, and widen the
sub-tick tracked set to every button `player_move` and the server's step loop
consume.

`subtick_plan.md` is the design this repairs; nothing in its format, wire encoding
or server re-simulation changes.

> **Status (2026-08-20): DONE. All seven steps landed, the probe is torn out,
> and the final measurement is in §4 step 7 and copied into `subtick_plan.md`.
> Two deviations from the text below are recorded at the steps they belong to
> (4 and 5), and one piece of follow-up work is named at step 5 — the server
> receives the ordering but does not act on it yet. Read those before concluding
> the plan lied.**

---

## 1. Why — four measurements, all reproducible

Everything below was measured on the dev machine with `cl_input_timing_debug 1`.
Do not re-litigate this from first principles; re-run the probe instead.

| Clock | Measurement | Verdict |
|---|---|---|
| SDL2 `event.timestamp` | 40 edges: age min 1.000ms, max 2.000ms, mean **1.125ms**, on 6.283ms frames | Pump time. Constant. Carries **no** input timing. |
| Windows `msg.time` | 300 motion messages: **276 deltas of 0ms**, 21 of 15–17ms | `GetTickCount` domain, 15.6ms granular — coarser than a frame, ~one tick. |
| `msg.time` + `timeBeginPeriod(1)` | 276 zeros / 21 jumps — statistically identical | No effect. Tickless kernel; `GetTickCount` no longer follows the multimedia timer. |
| **Raw-input thread + QPC** | 300 motion events: 159 deltas in 0.25–1ms, 141 in 1–3ms, **0 identical**, smallest **0.5892ms**, 0 dropped | Sub-millisecond, no quantization floor. The 0.59ms is the *mouse's* report interval, not a clock limit. |

**Root cause, one sentence:** every clock available to the game thread is read
during the pump, so all of them are pump clocks whatever their precision. Only a
thread *blocked waiting on input* can say when input arrived.

### Why not SDL3

Checked before committing to anything. SDL3's `WIN_GetEventTimestamp()` in
`src/video/windows/SDL_windowsevents.c` is:

```c
timestamp = SDL_MS_TO_NS(message_tick);   // message_tick comes from msg.time
timestamp += timestamp_offset;
```

Every `SDL_SendKeyboardKey` / `SDL_SendMouseButton` / `SDL_SendMouseMotion` call
passes it. So SDL3 gives nanosecond-*typed* values derived from the 15.6ms clock
measured above — **effective resolution would go from ~6.3ms (one frame, today) to
15.6ms (one tick)**. Migrating for this reason would lose ground. Migrate for other
reasons if you like; it does not solve this.

---

## 2. What exists right now

Probe state, all reachable via the `cl_input_timing_debug` `@Client` cvar
(`src/shared/cvars/cvars.def`). ~~**Nothing is wired into gameplay yet** — the
game still runs entirely on SDL's edges.~~

**This table is now HISTORY.** Everything in it is deleted, the cvar with it, and
the thread is the edge source with SDL's transitions as the fallback behind
`input::raw_input_is_active()`. The table stays because restoring the probe is
the only way to re-run the measurement, and this says what it was.

| File | State |
|---|---|
| `src/client/raw_input_win32.{hpp,cpp}` | **Keep.** The thread, the ring, the API. Production-shaped already. |
| ~~`input.cpp` — `windows_message_timing_hook`, `timeBeginPeriod(1)`, msg.time histograms~~ | **DELETED.** It measured SDL's stamps; §1 records what it found, and it could not outlive the field it read. |
| ~~`play_state.cpp` — the `[raw-input]` motion-delta block~~ | **DELETED.** It started the thread from inside a debug branch, which step 6 replaced with a real lifecycle. |
| ~~`play_state.cpp` — the `[input-timing]` block~~ | **DELETED at step 7**, after it reported the numbers below. It was rewritten first to report per-edge FRACTION and arrival spacing rather than SDL stamps. |
| ~~`cl_input_timing_debug` cvar~~ | **DELETED** with the probe above. |

`raw_input_win32.cpp` already does the load-bearing part: `QueryPerformanceCounter`
is read **before the message is inspected**, immediately on return from
`GetMessageW`. Anything moved above that read becomes latency measured instead of
input.

---

## 3. Target design

The split established in `input.hpp` stays, and this sharpens it:

```
LEVELS  ── SDL ──▶ is_key_down / is_mouse_down, sampled once in new_frame().
                   One defined instant. The resync source for lost transitions.

TIMES   ── raw ──▶ frame_input_edges(), stamped on arrival by the input thread.
           input   Microsecond resolution. The only thing sub-tick reads.
```

Neither derives from the other; that is deliberate and is what makes the
lost-transition check meaningful (see `play_state.cpp`, the `live_tracked_buttons`
comparison).

### Scope: widen `Button::Subtick_Tracked` to everything the tick consumes

Today it is six bits (`player_move.hpp:47`). The argument for widening is
**ordering within a tick**, and it is real: the server applies weapon switches from
`buttons_pressed_this_tick` (`server_impl.cpp:1149`) and fires from
`step.buttons & ~buttons_entering_step & Button::Fire`. Switch-then-fire and
fire-then-switch inside one tick are different outcomes, and today they are
indistinguishable.

Target set:

| Bit | Input | Was tracked | Notes |
|---|---|---|---|
| `Forward` / `Backward` / `Left` / `Right` | W A S D | yes | |
| `Jump` | Space | yes | |
| `Fire` | Mouse left | yes | |
| `Key0`–`Key9` | 0–9 | **no** | ordering vs `Fire` is the reason |
| `Zoom` | Mouse right | **no** | see decision D1 — it is a client-derived *state*, not a raw edge |
| `Reload` | R | **does not exist** | new bit 18; `P` squats on bit 17 |

Once every consumed bit is tracked, the `buttons & ~Button::Subtick_Tracked`
fallback in `play_state.cpp` (two sites) has nothing left to carry and **deletes**.
That simplification is a deliverable, not a side effect.

> Superseded by D1, which keeps `Zoom` a state bit. The merges stay, carrying
> exactly that one bit, and both sites now say so. See step 5.

### Budget

`MAX_SUBTICK_EDGES = 8` per tick, `SUBTICK_SLOT_COUNT = 64` (`subtick.hpp`). A
human produces 1–3 transitions in a 16.7ms tick even across a wider set, so 8
stands. The existing overflow `log_warning` is the guard — if it starts firing in
normal play, raise the cap rather than silently dropping timing.

---

## 4. Steps

Each step is independently verifiable. Do not batch them.

### Step 1 — Clock mapping

Replace the `age_seconds / real_frame_seconds` clamp
(`play_state.cpp`, the edge fold) with a real span.

- Record QPC once per frame at the same point `now_milliseconds` is read today.
- Keep the previous frame's value; `[previous, current]` is the span the
  accumulator's `dt` advance represents.
- An edge's fraction is `(arrival - span_start) / (span_end - span_start)`.
- **The clamp goes away.** It exists only because the age could exceed the frame;
  with real span endpoints an in-range arrival is in range by construction. An
  out-of-range arrival is now a bug worth a `log_warning`, not something to
  silently saturate to 0.

Verify: `[input-timing]` per-edge fractions spread across `0..1` instead of
clustering at ~0.82.

**Landed.** The span lives in the input layer (`input::frame_arrival_span()`,
both endpoints read in `new_frame`) rather than in `play_state`, because the
drain that defines the window is there. `cl_timescale` fell out of the
computation entirely: the fraction is unitless, so it multiplies onto the
already-scaled accumulator `dt` with nothing left to correct.

### Step 2 — VK mapping

Raw input reports VK codes; `input.cpp`'s tables are `key_t` ↔ SDL scancode. Add a
`vk_to_key` table beside the existing `key_mappings` one, built from the same
bidirectional-row pattern so the two cannot drift.

Only the tracked set needs rows: `W A S D`, `VK_SPACE`, `0`–`9`, `R`, plus mouse
buttons which `raw_input_win32.cpp` already reports as indices matching
`mouse_button_t`.

Verify: the probe's `[raw-input] key code N down/up` lines name the right keys.

**Landed** as `vk_mappings` / `g_virtual_key_to_key` in `input.cpp`, beside
`key_mappings` and built the same way. The codes are numeric literals rather
than `VK_*` names so the table survives a non-Windows build. A second table
falls out of the same rows — `g_key_is_raw_reportable`, which is what stops a
focus-regain resync pushing a down-edge for a key raw input will never send a
release for.

### Step 3 — Focus gating

`RIDEV_INPUTSINK` delivers while the game is not foreground. Drop raw edges unless
the game window has focus.

Do it **in the drain, on the game thread, not in the input thread** — the input
thread must not learn about game state, and the game thread already knows whether
it has focus (`gameplay_input_allowed` and the `input_edges_are_live` park logic).
Losing focus already records a release-everything edge; that path stays and is what
makes this safe.

Verify: alt-tab, type, return. Character does not move; no lost-transition warning
on return (the resample path covers it).

**Landed** in `drain_raw_input`. Losing focus releases everything held and
discards the backlog; regaining it discards the backlog and *then* resyncs from
the levels, pushing down-edges for whatever is still held. Both are stamped at
the span end. The drain runs after `new_frame`'s level snapshot precisely so the
resync reads this frame's levels.

### Step 4 — Swap the edge source

`frame_input_edges()` is filled from the raw-input drain instead of from
`process_sdl_event`. SDL's `process_sdl_event` keeps filling the level arrays and
the shortcut queue; it stops pushing `input_edge_t`.

`input_edge_t` gains the QPC arrival field and loses
`timestamp_milliseconds`. Everything downstream — the fold, `record_transition`,
slot conversion, the wire, the server — is untouched.

Verify: gameplay is unchanged; the lost-transition check does not fire in normal
play.

**Landed.** `process_sdl_event` still fills the level arrays and the shortcut
queue, and keeps its edge push as the FALLBACK — live only while
`raw_input_is_active()` is false, stamping at the frame boundary because SDL has
no moment inside the frame to recover. One field, one code path downstream.

~~**Deviation, accepted:**~~ **FIXED, and the fix was not where it looked.** The
level snapshot reflects the last SDL pump while the drain cuts a few
microseconds earlier in the same frame, so an arrival landing inside that gap
surfaced as a lost-transition warning and a resync that cost that press its
sub-tick position.

Aligning the two reads cannot fix it — they are two independent readers cutting
the timeline at two instants, and no ordering merges them (drain-before-pump
puts the levels ahead, drain-after puts the edges ahead). The fix is that the
CHECK was wrong to compare against one instant. The pump sits between the
previous drain and this one:

```
drain N-1 ....... pump N-1 ....... drain N
|                 |                |
live_tracked      `buttons`        after this frame's edges
```

so the poll is **bracketed**, and both ends are honest readings of the same
input — which end it matches says only which side of the pump the edge landed
on. Matching NEITHER is the real failure. That is exact: zero false positives,
no deferral, no new state, and it still catches a genuinely lost transition on
the frame it happens.

### Step 5 — Widen `Subtick_Tracked`

Add `Reload` (bit 18) to `player_move.hpp`, widen `Subtick_Tracked` to the table in
§3, extend `subtick_button_for_input_edge`.

Then **delete** the `buttons & ~Button::Subtick_Tracked` merges in `play_state.cpp`
— there are two, one in `buttons_before_tick` and one in the per-edge
`buttons_after`. If either still has bits to contribute, the set was not widened
far enough; that is the check.

Verify: `subtick_test` still passes. Add a case for switch-then-fire vs
fire-then-switch inside one tick resolving differently.

**Landed, with one deviation.** `Reload` is bit 18 and the ten weapon keys are
tracked. `test_ordering_within_a_tick_is_preserved` is the new case, and it
asserts the thing that makes the argument: both orderings end the tick in the
*same* state — which is why tick granularity could not tell them apart — while
the press slots and the middle step differ.

**The two `buttons & ~Button::Subtick_Tracked` merges do NOT delete.** D1 keeps
`Button::Zoom` a state bit, so the merges still have exactly one bit to carry.
What survives of the deliverable is the narrowing: both sites and
`Subtick_Tracked` itself now name what is left instead of saying "whatever the
set does not cover".

**Still open, deliberately out of scope:** the server now RECEIVES the ordering
but does not act on it. `server_impl.cpp` applies weapon switches from
`subtick_rising_edges` across the whole tick, before the step loop, so
switch-then-fire and fire-then-switch still resolve identically. Moving the
switch inside the step loop is a server behaviour change; the wire carrying the
information is its prerequisite, and that is what landed here.

### Step 6 — Lifecycle

`raw_input::stop()` exists and nothing calls it. Wire `try_start()` into client
init and `stop()` into shutdown. On `try_start()` failure, log and fall back to
SDL-sourced edges — one frame coarser, still playable, never silent.

**Landed** as `input::init()` / `input::shutdown()`, called from `client::init`
/ `client::shutdown` rather than as bare `raw_input::` calls at the launcher:
the input layer is what has to know which source is live, and it is the half
that owns the fallback. `init()` also fixes the arrival clock frequency — the
thread's QPC frequency when it started, `QueryPerformanceFrequency` when it did
not, `steady_clock` nanoseconds off Windows.

### Step 7 — Verify, then tear out

Re-run every measurement in §1 through the probe. Expected: per-edge fractions
uniform across the frame, arrival deltas sub-millisecond. Then delete everything in
§2 marked *delete*, and record the final numbers in `subtick_plan.md` so nobody
re-derives this.

**Done. The measurement, 40 real edges through the shipping path:**

```
[input-timing] ===== VERDICT over 40 edges (raw-input thread) =====
[input-timing] fraction min 0.0121  max 0.9856  mean 0.5068
[input-timing] smallest gap between arrivals 0.2078 ms (one sub-tick slot is 0.26 ms), 0 raw events dropped
```

Uniform across the frame — mean 0.5068 is what a uniform distribution on `0..1`
looks like, against the ~0.82 cluster SDL's stamps produced. And 0.2078ms
between arrivals is **finer than a slot**, which is the first time the 64-slot
grid rather than the input is the limit.

(The probe printed both of its interpretation lines regardless of the numbers;
the "fractions clustered" line is the one that did NOT apply. That is a wart in
a tool that no longer exists.)

Torn out afterwards: the `[input-timing]` block, the `raw_input_win32.hpp`
include it was the only user of in `play_state.cpp`, and the
`cl_input_timing_debug` cvar. Numbers copied into `subtick_plan.md` under
"Where the timestamps actually come from".

---

## 5. Decisions still open

**D1 — Zoom. DECIDED as recommended.** The raw right-button edge drives the
toggle now (`play_state.cpp`, the zoom block) and `Button::Zoom` stays a state
bit. That fixed a real bug in passing: `is_mouse_pressed(Right)` compares
frame-START levels, so a click that both pressed and released inside one frame
toggled nothing at all.

The original text:

**D1 — Zoom.** `zoom_active` is a client-side *toggle* derived from
`is_mouse_pressed(Right)` and sent as a STATE (`client_context.hpp`, the
`zoom_active` comment). Sub-tick timing on it only matters once the server charges
accuracy or speed per-step. Recommend: track the raw right-button edge, keep the
toggle derivation on the game thread, and leave `Button::Zoom` a state bit. Revisit
if scoping ever costs anything mid-tick.

**D2 — Reload. DECIDED as written** — bit 18 exists and is tracked, with no
server behaviour behind it.

**D2 — Reload.** `R` has no bit and no server handling today. Adding the bit is
cheap and makes the ordering argument complete; adding the *behaviour* is out of
scope for this plan.

**D3 — Non-Windows.** `raw_input_win32.cpp` has a stub `#else` branch returning
`nullopt`, so step 6's fallback is the whole story on other platforms. Fine while
the project is Windows-only; note it rather than build a Linux path speculatively.

---

## 6. What this does not change

The 64-slot grid, `subtick_input_t`, `try_record_subtick_state`'s fold, the wire
encoding in `subtick_codec`, the server's step loop, lag compensation. All of it
was already correct and already finer than the data feeding it — which is why the
fix is confined to where timestamps come from.
