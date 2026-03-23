# Client-Side Prediction & Reconciliation

## Overview

The client runs player movement physics locally (client-side prediction) so input feels instant. The server is authoritative — when the server's state disagrees with our prediction, we correct.

## How it works

1. **Predict**: Each server tick, the client runs `player_move()` locally and saves the input + result into a ring buffer (`pending_commands`).
2. **Send**: The same input is sent to the server as a `C2S_PlayerMoveCommand`.
3. **Receive**: The server sends back entity state + the last command number it processed.
4. **Repredict**: Starting from the server's authoritative position, the client replays all unacknowledged commands (server_ack+1 through current). This is the "reconciled" position.
5. **Correct**: Snap `player_position` and `player_velocity` to the reconciled values. Always. Physics must be correct.

## Visual Smoothing (Visual Error Offset)

Snapping the physics position causes visible pops if the camera follows it directly. Instead:

- On correction, compute `visual_error_offset = old_position - reconciled_position` (the "debt").
- Render the camera at `player_position + visual_error_offset`.
- Each frame, decay the offset: `visual_error_offset *= exp(-SMOOTH_SPEED * dt)`.
- Physics always runs from the corrected position — only the camera is smoothed.

This is frame-rate independent (exponential decay) and doesn't compound error like the old lerp approach did.

### Constants

| Name | Value | Purpose |
|------|-------|---------|
| `SNAP_THRESHOLD` | 5.0 | If error exceeds this, hard snap with no visual offset (teleport) |
| `SMOOTH_SPEED` | 16.0 | Exponential decay rate. Higher = faster visual correction (~60ms half-life at 16) |

### Why not lerp the physics position?

The old approach (`player_position += error * 0.1`) meant physics started from the wrong spot next frame. Each prediction then diverged again, causing oscillation — especially during jumps where client/server can disagree on grounding by 1-2 ticks.

## Debug HUD

Two lines in the play mode HUD:

- **reconc err** (red) — raw physics prediction error magnitude + XYZ breakdown
- **vis offset** (yellow) — current visual smoothing offset being applied to the camera

If `reconc err` spikes during jumps, it means client/server disagree on grounding state. The visual offset absorbs this so the player doesn't see it.
