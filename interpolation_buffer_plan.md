# done

# Remote interpolation — the delay buffer, and what the scheme does today

Drawing a remote player between two snapshots is right. Slaving the playback
clock to *packet arrival* is what makes it pop, and that is what the client does
now. This is the fix, and it is independent of `subtick_plan.md` — but it should
land first, because sub-tick's shot timestamps refine an
`interpolation_fraction` that is currently jitter-dominated. Refining a number
that does not mean anything yet buys nothing.

Status: **LANDED** (2026-08-19). Steps 1-4 are in; step 5 is sub-tick's.
What shipped differs from what is written below in two places, both found by
the step-2 test and both recorded at the end under "What the instrument
changed".

---

## What is there now, precisely

A **zero-delay, arrival-phase-locked, two-snapshot** interpolator. Three lines
carry the whole scheme:

```cpp
// on snapshot arrival -- play_state.cpp:796-815
remote_player.snapshots[0] = remote_player.snapshots[1];  // old target -> new source
remote_player.snapshots[1] = { ...just arrived... };
ctx.replication.interpolation_time = 0.f;                 // <-- the bug, in one line

// every frame -- play_state.cpp:1250, 1269
const float t = ctx.replication.interpolation_time / interp_duration;
ctx.replication.interpolation_time += dt;
```

The playback clock is **reset to zero on arrival**. So the client's render
timeline is not a clock at all — it is a phase measured from the last datagram,
and datagrams are jittery. Everything below follows from that one fact.

**Early arrival → it skips.** Suppose the lerp has reached `t = 0.6` between A
and B when C arrives. Now `snapshots[0] = B` and `interpolation_time = 0`, so
the very next frame draws at **B**. The rendered position jumps forward by 40%
of a snapshot interval in one frame, with nothing to smooth it.

**Late arrival → it slips.** `t` runs past 1, `lerp_clamped` pins it, and the
remote player **freezes** at B until the packet lands. The comment at
`play_state.cpp:1246-1249` already concedes this: *"there is no interpolation
delay buffer, so t reaching 1 is the routine case every time a snapshot is even
slightly late, not an edge case."*

Neither is an edge case. Both happen most snapshots, because there is no slack
anywhere in the scheme to absorb a millisecond of jitter.

**A third defect, already written down.** `interpolation_time` is a single
global, reset inside the per-player loop, so one phase describes every remote
entity. `todo.md`'s "Snapshot smoothing for rockets and physics bodies" entry
flags it: correct only while everything arrives in the same packet. The buffer
below fixes it for free, since each player gets its own ring — and that entry
should be re-pointed here rather than solving it a second way.

---

## The diagnosis in one sentence

**The client needs a clock, and it has a phase.** A clock runs at a steady rate
and can be *positioned* relative to another clock; a phase only knows how long
since the last thing happened. Every symptom above is a consequence of not being
able to ask "where should I be right now" independently of "when did the last
packet land".

---

## The design

### Render time lives in SERVER TICK SPACE, not in seconds-since-arrival

The one representational change; everything else is downstream of it. The client
keeps a `float cursor_tick` — a *fractional* server tick it is currently drawing
— advanced by `dt * server_tickrate` every frame and **never reset**.

Snapshots already carry `server_tick`. So drawing becomes: find the two
snapshots bracketing `cursor_tick`, lerp by the fraction between their ticks.
Arrival time stops entering the calculation entirely, which is the point.

### A ring per player, not two slots

`Remote_Player_State::snapshots[2]` becomes a small ring (8 is ~133ms at 60Hz,
comfortably more than any delay worth running). Two slots cannot hold a buffer:
an early arrival has nowhere to sit but on top of the snapshot still being drawn
through, which *is* the pop.

Per-player rather than global, which retires the "one bracket describes every
target" fragility rather than documenting it again.

### The delay is what buys the slack

`cursor_tick = newest_received_tick - cl_interpolation_delay_ticks`. A new
cvar, defaulting to **2** (~33ms at 60Hz).

That number is the entire jitter and loss budget, and it is a straight trade:
every tick of delay is a tick of extra latency on what you see of everyone else,
and a tick of extra rewind the server must be willing to do. 2 covers a single
dropped snapshot; 3 covers two. Start at 2 and read telemetry before moving it.

**The unit is TICKS, not milliseconds**, which is the opposite of Source's
`cl_interp`. A tick is the snapshot cadence, so "2" means "two snapshots of
slack" — the quantity that actually matters — and it stays meaning that if the
tickrate changes. A millisecond value silently becomes a different number of
snapshots the moment `sv_tickrate` moves. Same call `pm_friction`'s `exp` and
`g_gravity`'s parabola made in `subtick_plan.md`: keep the reading that survives
a tickrate change.

Do not spell it `cl_interp`. It is a *delay*, not an interpolation, and the
abbreviation is inherited from a different engine rather than chosen.

### Drift is corrected by TIME-SCALING, not by snapping

The client and server clocks run at slightly different rates, so `cursor_tick`
drifts from `newest_received_tick - cl_interpolation_delay_ticks` even with
zero jitter. Snapping when the gap grows reintroduces exactly the pop this
exists to delete.

Instead, advance `cursor_tick` at `dt * server_tickrate * rate`, where `rate` is
nudged within roughly `[0.95, 1.05]` to close the gap. A 5% speed difference is
imperceptible on someone else's motion; a 30ms jump is not. **Snapping is
reserved for things that are already discontinuities** — a map change, a
respawn, an entity id change (`play_state.cpp:788-791` already resets on that
one), a gap larger than the ring.

### Buffer dry is an explicit, rare choice

With a delay of 2, "no snapshot newer than `cursor_tick`" means loss exceeding
the budget. Then, and only then, pick one — and **freeze** is the right pick,
not extrapolation. Extrapolating a player who is about to change direction
manufactures a position they were never at, and the server's lag compensation
cannot agree with it: `try_pose_players_across_bracket` lerps between two real
frames and has no way to reproduce an extrapolated pose. Freeze is wrong in a
way both sides agree about. Log it — a dry buffer is real packet loss, not a
rendering detail.

---

## What this costs, stated plainly

**You are deliberately adding `cl_interpolation_delay_ticks` of latency to
everything except your own body.** That is not a side effect, it is the
mechanism: the slack is the product. Lag compensation is what pays it back — the server rewinds to the
bracket you drew, so the delay costs you nothing on a shot even though it costs
you something on a reaction.

**It eats into the rewind budget, and that needs a decision.**
`sv_max_rewind_ticks` is 12 (~200ms), and today it covers only RTT/2 plus
jitter. After this it also covers `cl_interpolation_delay_ticks`. At 2 that is
17% of the budget — fine. At 6 it would not be. Either the cap rises or the delay stays
small; that is a fairness call (`lag_compensation_def.md`), not a tuning knob,
because the cap is what bounds how long a victim can be shot behind cover.

**`classify_bracket` needs no change** — `towards_tick <= held_snapshot_tick`
stays true, just with more slack than before. Worth an assertion rather than an
assumption.

---

## The order

### 1. Extract the interpolator so it can be tested at all

Today it is inline in `play_state.cpp::update`, tangled with packet handling
above it and camera resolution below it. Nothing about lerping two poses needs a
socket, a device or a window.

Pull it into `client/remote_interpolation.{hpp,cpp}`: snapshots and a render
clock in, poses out. Values in, values out — the shape `hitscan.hpp` and
`lag_compensation.hpp` already use, and for the same reason.

### 2. Build the arrival-schedule test — do not skip this

The counterpart to `player_move_step_invariance_test`, and the same argument
applies: it is an INSTRUMENT before it is a guard. Feed scripted arrival
schedules — clean 60Hz, ±5ms jitter, one dropped snapshot, two consecutive,
reordered, a 200ms burst — and assert on the *rendered* output:

- **continuity**: frame-to-frame rendered movement never exceeds what the
  player's own speed allows. That is the pop, written as an assertion.
- **monotonicity**: `cursor_tick` never goes backward.
- **bounded lag**: it stays within a tick or two of the intended delay across
  the whole schedule, so drift correction is proven to converge rather than
  merely to run.
- **freeze, not fabricate**: on a dry buffer the position holds and the log
  fires.

Without this, "is it smooth now" is a judgement made by squinting at a moving
image, and every regression is invisible until someone notices in a playtest.

### 3. The buffer itself

Ring, interpolation cursor, delay, drift correction, dry policy. One change, guarded by
step 2.

### 4. Rewire `interpolation_fraction` to be DERIVED, not recomputed

Today the reported bracket (`play_state.cpp:1100-1117`) and the drawn bracket
(`play_state.cpp:1240-1250`) are two separate computations off two different
sources — global ticks versus per-player snapshot ticks — that agree only
because delta reconstruction happens to fill every player into every frame. The
comment claims "by construction"; it is actually by coincidence.

After step 3 there is one interpolation cursor, so the reported bracket must be *read
off it* rather than recomputed. That also retires both "accepted inaccuracies"
listed at `play_state.cpp:1092-1099`: the fraction stops being last frame's, and
it stops being global.

### 5. Only then, sub-tick's edge-sampled fraction

`subtick_plan.md` step 3 wants the fraction sampled at the input edge rather
than at command-build time. That is a sub-millisecond refinement and worth
having — but worth having *after* the number it refines is continuous. Doing it
first would be polishing jitter.

---

## What this does NOT do

**It does not touch prediction.** Your own body is predicted forward and
reconciled; nothing here is in that path. The two halves of the client's split
timeline stay split — see `subtick_plan.md`, "Two clocks".

**It does not smooth rockets or physics bodies.** Those are the `todo.md`
Networking entry, and they should be built *on* this once it exists — a rocket
wants extrapolation along `velocity` rather than a lerp, but it wants the same
interpolation cursor. Do not give it a second one.

---

## What the instrument changed

Step 2 was built before step 3, as written. It earned that three times.

### `classify_bracket` DID need something after all

The claim above -- "`classify_bracket` needs no change; `towards_tick <=
held_snapshot_tick` stays true, just with more slack" -- is false, and the line
that follows it ("worth an assertion rather than an assumption") is what caught
it. `towards_tick` is `floor(cursor_tick) + 1`, so any frame where the clock has
outrun its newest sample reports `newest + 1`, and the server answers `Unheld`
and falls back to present-tick poses. That is not rare: it is every late
snapshot, and every frame of every dry buffer -- so lag compensation would have
dropped out during exactly the stalls that need it, invisibly, on the shooter's
side.

`bracket_at` now pins the bracket to `newest_received_tick`, collapsing it to
`(newest, newest, 0)` when the clock is past it. Not a fallback: a dry buffer
draws FROZEN at the newest sample, and that collapsed bracket is the single
moment being drawn. The invariant is now a property of the function rather than
of timing, and the test checks it on every frame of every schedule rather than
once on the easy case.

### The snap has to be ONE-DIRECTIONAL

The design says to snap on "a gap larger than the ring". Implemented literally —
`|newest - delay - cursor_tick| > ring` — the burst-stall schedule failed on
`cursor_tick` going *backward*.

The reason is worth keeping. During a stall the clock keeps running while
`newest_received_tick` stands still, so the two are far apart by exactly the
stall length. When the backlog lands it arrives **oldest first**, and the first
of those arrivals proposes a target a whole stall behind where the clock is — so
a symmetric snap yanks the clock back to the front of the burst, which is both a
visible rewind and precisely the pop this work exists to delete. A few arrivals
later the offset would have been right on its own, because the clock and the
server's tick count advanced by the same amount.

So the snap fires only when the clock is too far **behind** its target (a long
pause, or the delay cvar being cut). A clock that has run *ahead* is the dry
buffer, and the dry buffer is self-correcting by construction. That is the
mechanical half of "let the clock run through a stall", and the reason that
decision is not merely the simpler of two options.

### Delay 2 covers one dropped snapshot *exactly*, with no margin

Stated as "2 covers a single dropped snapshot" above. The measurement says: a
drop puts the next arrival two tick-intervals away, and at delay 2 the clock
covers exactly two tick-intervals between arrivals — so **one drop lands on the
boundary**. It grazes dry for a frame or two rather than being absorbed.

Not a defect, and not a reason to move the default: it is the same place Source
sits at `cl_interp_ratio 2`, and the graze costs a frozen frame rather than a
pop. But "2 covers one drop" was optimistic by one, and the honest statement is
**3 covers one drop comfortably**. `remote_interpolation_test` asserts the graze
rather than asserting it away, so the number is measured rather than assumed if
telemetry ever says stalls are common.

---

## Where it lives

```
client/remote_interpolation.{hpp,cpp}   the clock, the ring, the sampler, bracket_at
client_context.hpp                      replication.interpolation_cursor (one per connection)
                                        Remote_Player_State::interpolation (one ring each)
play_state.cpp                          three call sites: push on arrival,
                                        advance once per frame, read the bracket
cvars.def                               cl_interpolation_delay_ticks, cl_interpolation_debug
test/remote_interpolation_test.cpp      arrival schedules
```

Deleted: `Remote_Player_Snapshot`, `Remote_Player_State::snapshots[2]` /
`snapshot_count`, `replication_t::interpolation_time`,
`replication_t::previous_snapshot_tick`, and the parallel bracket derivation in
the move-command builder.

`replication_t` is cleared wholesale by both client resets, so a map change
starts the clock over with no case anywhere for it — the discontinuity is handled
by the reset that was already there.
