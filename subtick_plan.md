# Sub-tick movement — the plan, and what has to be true first

Quantizing "when the key went down" to a 16.7ms tick boundary is a **modeling
error**, not a simplification: the press happened at a time, and the tick grid
is an implementation detail of the simulator. So sub-tick is the right end
state. It is also third in line, and doing it first is how it goes badly.

Status: **all three steps are done.** Friction (2026-08-18), gravity
(2026-08-19), the accelerate/friction ordering (2026-08-19), and the
step-invariance test that found the third one
(`player_move_step_invariance_test`, 2026-08-19). Every arithmetic divergence
that was going to perturb a split tick is gone; what is left diverges on
purpose. **Step 3 landed 2026-08-19** — the format, the client's input edges,
the server's split move loop and the shot's own timestamp; see "What step 3
actually built" at the bottom for what is in the tree and what is deliberately
still whole-tick.

The test found two things that changed the list below: a fifth arithmetic item
nobody had written down (accelerate and friction do not commute), and that
item 3 as originally written is wrong — `accelerate`'s clamp composes exactly
on its own.

---

## Why the order matters

Sub-tick means one tick sometimes runs as two or more movement steps. That is
only safe if you know what changes when a step is split — and in `player_move`
today, five separate things do. They are not the same kind of thing, and only
three of them are fixable.

**Arithmetic — a per-step function that is only a first-order approximation.
Fixable, and worth fixing on its own merits:**

1. **Friction** was `speed * (1 - friction*dt)`, exponential decay computed
   linearly. Two half-steps charge the second one against an already-reduced
   speed, so it composes wrong. **Fixed 2026-08-18**: the branch above
   `pm_stopspeed` is `std::exp(-pm_friction * dt)`, which composes exactly
   because `exp(a)exp(b) = exp(a+b)`. The branch *below* `pm_stopspeed` was
   always exact — `control` becomes the constant `pm_stopspeed`, so the drop
   stops depending on the thing it is changing. That branch is a deliberate
   deceleration floor and must stay linear; pure exponential decay never
   reaches zero.

   Note this changed what `pm_friction = 6` *means*: 0.9 per 60Hz tick before,
   `exp(-0.1) = 0.9048` now. Deliberately not retuned — the exponential reading
   is the one that survives a tick-rate change.

2. **Gravity + position** is semi-implicit Euler: `v -= g*dt` then `p += v*dt`,
   integrating position with the *end* velocity. Velocity sweeps linearly
   across the step, so the true distance is the average of the endpoints:

   ```
   true:            p0 + v0*dt - 0.5*g*dt^2
   today (end v):   p0 + v0*dt - 1.0*g*dt^2
   two half-steps:  p0 + v0*dt - 0.75*g*dt^2
   ```

   **Fixed 2026-08-19**, as leapfrog rather than as a literal average: half the
   gravity step lands before the position integration and half after, so
   position integrates with the midpoint y velocity — identical arithmetic,
   no second velocity variable, and no signature change to `step_air_move`.
   The returned velocity is still `v0 - g*dt`, so the grounded check, the land
   snap and the post-move clips see exactly what they saw before.

   The feel change was taken deliberately: **jump apex is ~5% higher** (45.6
   units, was 43.3), and every fall carries `0.5*g*dt` ≈ 6.7 units/s less
   constant sink. `g_gravity` was not retuned — the parabola is the reading that
   survives a tickrate change, the same call friction's `exp` made.

2b. **Accelerate and friction did not commute.** Not originally on this list;
   the test found it. Each ground step runs friction and *then* accelerate, so a
   split charged friction against speed the previous sub-step had just added:
   two half-steps ended ~1.3 units/s slower from 100 at full forward input.
   Neither operator was inexact any more — applying them *alternately* was what
   was wrong, and the coupled system has a closed form too:

   ```
   exact:  v(dt) = v0*exp(-k*dt) + (A/k)*(1 - exp(-k*dt))   A = pm_ground_acceleration * pm_maxspeed
   before: v <- v*exp(-k*dt/N) + accel*(dt/N)*W   per sub-step
   ```

   **Fixed 2026-08-19**, and the fix is a *duration*, not a new operator. The
   closed form is `v0*decay` — which `apply_friction` already returned — plus
   the same `accelerate` integrating over `(1 - exp(-k*dt))/k` instead of `dt`.
   So `apply_friction` now hands back that weighted time alongside the decayed
   velocity (`friction_step_t`), and `my_walk_move` feeds it to `accelerate`.
   Two halves of the weighted time sum to the whole, which plain `dt` does not:
   `t(h)*(1 + exp(-k*h)) = (1 - exp(-2*k*h))/k`.

   The clamp survives it exactly, which is the part worth checking rather than
   assuming: `min(d*f + c, W)` composed with itself is `min(d*f² + c*f + c, W)`
   whichever side of `W` each half lands on, because `c ≥ W*(1-f)` whenever
   `pm_ground_acceleration ≥ pm_friction`. A saturated projection stays pinned
   under any split — `test_ground_saturation_is_step_invariant`.

   Feel cost, taken deliberately like the other two: a tick gains
   `A*(1-exp(-k*dt))/k` where it gained `A*dt`, which at `pm_friction = 6` and
   60Hz is **4.8% less acceleration through the transient**. The clamp binds
   after ~7 ticks from a standstill, so top speed does not move; only the ramp
   does.

**Structural — a discontinuity, not an approximation. Not fixable, and mostly
should not be:**

3. **The maxspeed clip in `step_air_move`** — the one that actually
   re-projects. Above `pm_maxspeed` it normalizes the horizontal vector and
   rescales, so the *direction* entering the next `accelerate` depends on when
   the clip fired; a finer split keeps more lateral gain (vz 21.3 → 26.3 from
   400 units/s over one tick). **This is Quake air control**, and making it
   dt-exact deletes bunnyhopping and strafe-jumping.
4. **The clamp in `accelerate` is NOT one of these**, which is the correction.
   Its increment is `accel*h*wish_speed`, proportional to `h`, and
   `min(min(d+c, W)+c, W) == min(d+2c, W)` — a monotone increment through a
   `min` composes exactly. With a fixed wish direction the clamp is a *target*,
   so every subdivision saturates at the same `wish_speed`. It only looks
   step-dependent because the clip above re-projects the velocity it measures
   `add_speed` against. One mechanism, in the clip, not two.
5. **Collision, stepping, branch selection.** `resolve_collisions` pushes out of
   *measured* penetration, `pm_step_height` is a constant with no `dt` in it,
   and `grounded` / `pressing_into_wall` / `raised_blocks_wish` are discrete
   branches a different step size can flip outright. Geometry queries, not
   arithmetic. Nothing makes them dt-invariant.

**The insight that makes sub-tick tractable anyway:** you cannot make a clamp
dt-invariant, but you *can* put the step boundary exactly where the input
happened, so the clamp fires at the correct moment. That is the whole trick, and
it is what CS2 does — its stated reason for inserting an extra movement step at
the press timestamp is "to prevent friction-free acceleration time like in
CS:GO". For source 3, dt-dependence stops being a defect and becomes the
mechanism.

---

## The order

### 1. Fix the arithmetic that is wrong regardless — **done**

Friction, gravity and the accelerate/friction ordering. None of the three
needed sub-tick to justify it, and each one removed something that would
otherwise perturb every split tick. What remains dt-dependent in `player_move`
is items 3 and 5 only: the maxspeed clip, and the geometry branches. Both are
structural, both are guarded as DELIBERATE, and neither is a defect.

### 2. Build the step-invariance test — do not skip this

**Done**: `src/test/player_move_step_invariance_test.cpp`. One tick at `dt`
versus N sub-steps of `dt/N` over the real cvar defaults, one cause live per
scenario, each divergence asserted against the closed form that predicts it.
Every scenario is labelled EXACT / ARITHMETIC / DELIBERATE, and the DELIBERATE
ones assert that they *still diverge* — so "fixing" air control fails here
rather than in playtest.

`gravity_position_uses_endpoint_average` at the top of that file is the knob:
flip it when step 1's gravity fix lands and every gravity expectation follows.
It also retroactively covers the friction fix, which shipped with no test.

Scenario 7 was the ARITHMETIC one; with 2b fixed it is EXACT, and 7b was added
beside it to assert the clamp keeps pinning at `pm_maxspeed` under every
subdivision. Scenario 6 (ground position under decay) is the one ARITHMETIC
label left, and it is *not* on step 1's list: it is a quadrature error on
position, it never feeds back into velocity, and the trapezoid gravity fix does
not cover it because the integrand there is an exponential rather than a ramp.
It shrinks under a split instead of growing, so sub-tick makes it better.

This is the prerequisite for step 3, not a nicety. When prediction rubber-bands
after sub-tick lands, this test is the only thing that says whether the split is
at fault or the stair-stepping is. Without it every bug has two candidate
causes.

### 3. Sub-tick itself — **done, 2026-08-19**

With that list in hand, so each remaining divergence can be classified as
mechanism (source 3 — intended) or defect.

**The information is in the EDGE, not the state.** The move command carries
*held* axis values today, and a held value has no interesting timestamp. The
client has to timestamp SDL input events as they arrive and accumulate the
transitions into the command, rather than sampling state once when it builds
one. That is the real work, and it is client-side.

Then: the move loop runs N steps for one tick, split at those edges; prediction
splits identically (a disagreement about the split is rubber-banding, not a
rounding error, because of the clamps); shots carry their own timestamp and
compose with the lag-comp bracket.

**Wire format: a `uint32` slot index, not a `float` fraction.** CS2 quantizes to
64 discrete slots per tick (~0.24ms at 64Hz) rather than sending a real number,
and the reason is prediction determinism: the client predicts and the server
re-simulates, and those two runs must agree exactly. A float that differs by one
ULP feeds a `dt` that feeds a clamp — a discontinuity — and diverges the whole
prediction. A 6-bit integer cannot disagree with itself. It is also cheaper on
a message that ships every tick, and it bounds the work (no 0.0001ms sub-step
that costs a full pmove pass and moves nothing).

Resolution is not the constraint: 0.24ms is 0.12 units of travel at 500 u/s.
The thing being fixed is 16.7ms, which is 8 units.

---

## Two clocks, and why the sub-tick slot is not `interpolation_fraction`

They look alike — both are "a fraction of an interval" — and they answer
different questions, in the same way `held_snapshot_tick` and
`interpolated_towards_tick` do (see the comment in `proto/game.proto`).

```
interpolation_fraction   where between two PAST snapshots this client was
                         DRAWING REMOTE PLAYERS.  Backward-looking, remote.
                         Consumer: lag compensation, rewinding targets.

sub-tick slot            when inside the CURRENT predicted tick THIS CLIENT'S
                         OWN INPUT CHANGED.  Present, local.
                         Consumer: the movement integrator, splitting the step;
                         and prediction, reproducing that split.
```

Neither is a wall-clock time. Both are positions in tick-space — just different
tick-spaces, and the client is deliberately in both at once: it renders everyone
else *behind* the server while predicting its own body *ahead* of it. One press
therefore has two positions on two timelines, and neither is derivable from the
other.

They also have different denominators. `interpolation_fraction` is a fraction of
the gap between two snapshots the client happens to hold, which after packet
loss is not one tick. A sub-tick slot is always a fraction of exactly one tick.
The same 6 bits would mean different durations.

The split of responsibility, at the instant of a shot:

- **where THEY were** — lag compensation, from the bracket. Have it.
- **where I was, and when my action took effect** — sub-tick. Do not have it.

Both are halves of "the world at the moment I pressed". Lag compensation only
ever addressed the remote half.


---

## What step 3 actually built

`src/shared/subtick.hpp` is the format and the driver,
`src/shared/network/subtick_codec.hpp` the one place it meets a move command, and
`subtick_test` the guard on both. Both grammars live there, and the reason there are two is the interesting
part: the wire's is **strict** (an edge that breaks it is a client we did not
ship, so the command is refused rather than simulated), the client's recorder
**folds** (it is fed raw SDL transitions, whose resolution is coarser than a slot
and whose order is whatever the queue handed us). `split_input_per_tick_into_subtick_steps` is what both sides
run, and a command with no edges splits into exactly the one `tick_dt` step it
replaced — which is what let bots, `server_loop_test` and every other caller go
untouched.

**The client, which was the real work, as predicted.** `input::frame_input_edges`
is a third queue beside the two that were already there, and the difference is
that it carries RELEASES and a timestamp; the two old ones are presses without
one, because a menu does not care when inside the frame you clicked. The
placement is in **accumulator space, not wall-clock**: an edge's position is
`accumulator_at_frame_start + fraction_into_frame * dt`. Ticks consume the
pending edges in order and rebase the leftovers by `tick_dt`, exactly as the
accumulator itself is rebased.

> The fraction originally came from the edge's AGE against the frame's real
> duration, because SDL's clock and the accumulator share no origin. That is
> gone — see "Where the timestamps actually come from" below. It is now a ratio
> of a real span, which needs no calibration and no clamp.

Three things fell out of building it that were not on the list:

- **The poll is a frame stale, and that turns out to be a feature.**
  `input::new_frame` snapshots SDL's keyboard *before* the frame's events are
  pumped, so `is_key_down` is the state the previous frame's edges should have
  left behind. That makes the two comparable, and a disagreement means a
  transition never reached us — a KEYUP eaten by focus loss is the one that
  happens. Nothing else resamples any more, so without that check a lost release
  sticks a button down forever. It logs and resyncs.
- **The console is an edge.** Opening it releases every tracked button at the
  moment it opened rather than at the next boundary; closing it resamples,
  because the releases that happened while it held the keyboard were never read.
- **`cl_timescale` scales `dt` but not the input clock.** The age had to be
  measured against the frame's real duration and then mapped onto the
  accumulator's, which are the same number only at timescale 1. This one did not
  survive the span rewrite either: a fraction is unitless, so it multiplies onto
  the already-scaled `dt` with nothing left to correct.

**Six buttons were tracked** (`Button::Subtick_Tracked`: the four directions,
jump, fire) on the argument that everything else resolving 16.7ms late is
invisible, and that every button admitted costs a pmove pass on the server
whenever it moves. The set is wider now — the weapon keys and reload joined it,
not for feel but for ORDERING; see the raw-input section below.
`MAX_SUBTICK_EDGES = 8` is the sub-step budget, not a guess about typing speed:
it is what bounds the work one datagram can ask for, and the move budget bounds
the rate of datagrams rather than this.

**`buttons_bitfield` changed meaning** — it is the state at the START of the
tick, not at the moment the command was built. Everything that read it as "the
buttons now" had to move to reading the whole tick: `subtick_rising_edges` is
what weapon switching uses, and it sees a press and its release that both land
between two boundaries, which the old two-boundary compare could not.

**The shot got its timestamp, on both ends.** The server's fire path came out of
the move loop into `resolve_player_shot` and is called from inside the step loop,
after the step the press landed in — so the shot is taken from where the shooter
had actually reached, not from wherever the whole tick left them. And the client
now reads its interpolation bracket **at the moment of the press** rather than at
command-build time (`bracket_at(clock, ticks_before_now)`), because the world it
was drawing then is that fraction of a tick older. That is the same 16.7ms of
travel sub-tick exists to stop rounding away, on the other end of the shot.

### Still whole-tick, and fine

- **View angles.** One yaw/pitch per command, so a sub-step runs under the angle
  the whole tick did. A mouse delta has no edge to timestamp the way a key does —
  it is a rate, sampled per frame — so this needs a different mechanism than the
  one built here, and wants a reason before it gets one.
- **The bracket's phase.** `advance_interpolation_cursor` runs once per frame while the
  tick loop may run zero or more times, so the clock the bracket is read from is
  the frame's, not the tick's. The sub-tick offset above is a first-order
  correction on top of that, and it is bounded by one tick either way.
- **Bots.** No edges, one step, the simulation they always had.


---

## Where the timestamps actually come from

Everything above was correct about the FORMAT and wrong about the data feeding
it. The 64-slot grid resolves 0.26ms; the client was handing it edges resolved
to one frame, and the code claimed otherwise. `raw_input_plan.md` is the repair
and has the full argument. The short version and the numbers:

**Every clock the game thread can read is read during the pump, so all of them
are pump clocks whatever their precision.** Only a thread *blocked waiting on
input* can say when input arrived.

| Clock | Measurement | Verdict |
|---|---|---|
| SDL2 `event.timestamp` | 40 edges: age min 1.000ms, max 2.000ms, mean **1.125ms**, on 6.283ms frames | Pump time. Constant. Carries **no** input timing. |
| Windows `msg.time` | 300 motion messages: **276 deltas of 0ms**, 21 of 15-17ms | `GetTickCount` domain, 15.6ms granular — coarser than a frame. |
| `msg.time` + `timeBeginPeriod(1)` | statistically identical | No effect. Tickless kernel. |
| SDL3 | `WIN_GetEventTimestamp` derives from `msg.time` | ns-*typed* values off a 15.6ms clock. Migrating would LOSE ground. |
| **Raw-input thread + QPC** | 300 motion events: 0 identical, smallest **0.5892ms**, 0 dropped | Sub-millisecond, no quantization floor. |

`client/raw_input_win32.{hpp,cpp}` blocks in `GetMessageW` on a message-only
window and reads `QueryPerformanceCounter` **before it inspects the message**.
`input::init` starts it; failure is never fatal, and `process_sdl_event` keeps a
fallback that stamps at the frame boundary.

**The end-to-end result, measured through the shipping path** (40 real edges,
raw-input thread live):

```
fraction min 0.0121   max 0.9856   mean 0.5068
smallest gap between arrivals 0.2078 ms   (one sub-tick slot is 0.26 ms)
0 raw events dropped
```

Uniform across the frame, and the arrival spacing is now finer than a slot —
which is the first time the grid, not the input, is the limit. **Do not
re-litigate any of this from first principles; re-run the probe instead** (the
probe itself is deleted, so re-running it means restoring it from
`raw_input_plan.md` §2).

Two things changed shape as a consequence:

- **The fraction is a ratio of a real SPAN.** `input::frame_arrival_span()` is
  `[previous drain, this drain]`, both read at the same point in the frame, so
  an arrival inside it is in range *by construction*. The clamp is gone and an
  out-of-range arrival is a `log_warning`, because it would now be a bug.
- **`Button::Subtick_Tracked` widened** to the weapon keys and `Reload`. The
  argument is not feel, it is ordering: switch-then-fire and fire-then-switch
  inside one tick end the tick in the same state, so tick granularity could not
  tell them apart. The wire carries the distinction now;
  `test_ordering_within_a_tick_is_preserved` guards it.

---

## Acting on the ordering — **done**

The server applied weapon switches in one whole-tick pass *before* the step
loop, so every edge it had just paid a pmove pass to decode was thrown away at
the moment it mattered. `Button::Reload` was worse: tracked, stamped, shipped,
and read by nothing at all.

Every button ACTION now resolves inside the step loop, at `step.start_slot`.
That is why the server never calls `subtick_slot_of_press` — a press is what
*opens* a step, so the split already delivered the slot to the site that
consumes it. `test_the_step_a_press_opens_carries_its_slot` is the guard on that
identity, because if it ever stops holding, every cadence below silently rounds
back to the tick with nothing failing.

**The moment got a type.** `subtick_time_t` is `tick * SUBTICK_SLOT_COUNT +
slot` — one monotonic integer, with `subtick_seconds_between` and
`subtick_time_after` as the two directions. The fire-rate gate ran on
`tick_number - last_fire_tick`, which quantized every weapon's cadence to
16.7ms: the one gate deciding whether a shot happens at all was still doing the
rounding the rest of the feature exists to delete.

**It is deliberately not a wire type.** The wire has no grid finer than a tick —
one snapshot per tick — so a replicated sub-tick stamp costs bytes for a reader
that cannot use them. `Player_Entity::last_fire_tick` stays a tick and stays
`@Networked`, because its consumer is `weapon_fire_audio`, a change detector.
Beside it sits `last_fire_slot` (u8, server-only), which *refines* that stamp
rather than duplicating it: read together as `subtick_time(last_fire_tick,
last_fire_slot)`. A second whole stamp could disagree with the first; a
refinement cannot.

**Reload is the first action that outlives its input.** It spans ~120 ticks, so
the input for tick N carries nothing about a press at tick N-120 and retained
state is unavoidable. What is retained is the **deadline**, not the start:
the duration belongs to the weapon in hand *at the press*, and the weapon can
now change at a sub-tick moment inside the same loop. Storing the start would
leave "whose duration applies" to be re-decided every tick against a weapon that
is no longer the one being reloaded. Switching cancels the reload, which is the
ordering payoff the weapon keys were admitted to `Subtick_Tracked` for.

Completion is checked **twice, on purpose**. The fire path completes a due
reload itself at the trigger's slot, which is the authoritative answer and is
exact. An end-of-tick sweep completes the rest, which is the *replicated* answer
and is tick-granular because `ammo` rides a snapshot. Same split as above: the
simulation is exact, the wire is not, and neither pretends to be the other.

### Still to do

- **The rewind bracket is per-input, not per-press.** `resolve_player_shot`
  reads `interpolated_*` off the whole `C2S_ClientInput`, so a tick containing
  two trigger presses judges both through the bracket the client measured at the
  first. Unreachable today — the fire interval is longer than a tick for every
  weapon — and it becomes real the moment one is not.
