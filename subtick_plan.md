# Sub-tick movement — the plan, and what has to be true first

Quantizing "when the key went down" to a 16.7ms tick boundary is a **modeling
error**, not a simplification: the press happened at a time, and the tick grid
is an implementation detail of the simulator. So sub-tick is the right end
state. It is also third in line, and doing it first is how it goes badly.

Status: **step 1 done** for friction (2026-08-18) and gravity (2026-08-19),
**step 2 done** (`player_move_step_invariance_test`, 2026-08-19). One arithmetic
item is left, and it is the one the test found. Step 3 not started.

The test found two things that change the list below: a fifth arithmetic item
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

2b. **Accelerate and friction do not commute.** Not originally on this list.
   Each ground step runs friction and *then* accelerate, so a split charges
   friction against speed the previous sub-step just added: two half-steps end
   ~1.3 units/s slower from 100 at full forward input. Neither operator is
   inexact any more — applying them alternately is what is wrong, and the
   coupled system has a closed form too:

   ```
   exact:  v(dt) = v0*exp(-k*dt) + (A/k)*(1 - exp(-k*dt))   A = pm_ground_acceleration * pm_maxspeed
   today:  v <- v*exp(-k*dt/N) + accel*(dt/N)*W   per sub-step
   ```

   The discrete form converges *down* to the exact one, so today's tick
   accelerates fastest of all subdivisions. Same feel-change caveat as gravity:
   fixable, not free.

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

### 1. Fix the arithmetic that is wrong regardless

Friction and gravity: **done**. The accelerate/friction ordering (2b) is the
one left, and it is the weakest case of the three — the clamp binds before
terminal speed is ever approached, so the divergence is transient. None of the
three needs sub-tick to justify it, and each one removes something that would
otherwise perturb every split tick.

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

This is the prerequisite for step 3, not a nicety. When prediction rubber-bands
after sub-tick lands, this test is the only thing that says whether the split is
at fault or the stair-stepping is. Without it every bug has two candidate
causes.

### 3. Sub-tick itself

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
