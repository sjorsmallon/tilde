#pragma once
#include <algorithm>
#include <cstdint>

// How much movement a client is allowed to execute, and why it is a budget over
// TIME rather than a cap per tick.
//
// The exploit: every move in `incoming.moves` runs a full tick_dt of movement --
// as one `player_move` step, or as the several a command's sub-tick edges split
// it into (shared/subtick.hpp), which sum to the same tick_dt -- so a client
// that sends more move commands than the server ticks moves further per second
// than anyone else. Firing is interval-gated, so it is a movement exploit rather
// than a damage one.
//
// Sub-tick edges change what a credit COSTS, not what it buys: a command with
// the maximum edges asks for MAX_SUBTICK_EDGES + 1 pmove passes instead of one.
// That is CPU, and it is bounded by the edge cap rather than by this budget --
// which is the reason the cap exists at all.
//
// **A per-tick cap does not fix this**, which is the thing worth writing down. A
// speedhacker sending 3x the command rate at 60Hz sends 180 commands a second
// and the server ticks 60 times; a cap of 3 moves per tick admits all 180 of
// them. The rate is the exploit, so the rate is what has to be bounded.
//
// So: a client accrues ONE credit per server tick and spends one per executed
// move. Over any interval a client can therefore execute at most as many moves
// as the server ticked, whatever the arrival pattern. Credits accumulate up to
// `max_backlog` so a network stall is absorbed rather than punished -- the
// commands were honestly generated, they just arrived together -- and the cap is
// what stops a client from banking a minute of idle time and cashing it in as
// one enormous teleport.
//
// Two alternatives, and why they are worse:
//
//   Splitting the tick (run N queued moves at dt/N each) breaks prediction
//   outright. The client already predicted each of those commands at a full
//   tick_dt, and N depends on network jitter the client cannot observe, so the
//   two simulations can never agree. It also penalises an honest client for
//   packet loss: those N commands really are N ticks of input.
//
//   Queueing the excess for later ticks needs a per-client backlog that
//   survives clear_incoming, and it only defers the question -- the queue needs
//   its own cap, at which point it is this with more storage.
//
// A move that finds no credit is DROPPED, not deferred. The client sends one
// datagram per command with no redundancy (play_state.cpp), so a dropped
// command is gone and the client rubber-bands the distance it predicted. That
// is the intended penalty, and `max_backlog` is set high enough that an honest
// client reaches it only after a stall long enough to be visible anyway.
namespace server
{

// Called once per connected client per tick, before any of its moves run.
[[nodiscard]] inline int32_t grant_move_credit(int32_t credits,
                                               int32_t max_backlog)
{
  return std::min(credits + 1, std::max(max_backlog, 1));
}

// Spends one credit if there is one. `false` means this move must be dropped.
// Not `try_` + optional: the credit count is the caller's own storage being
// decremented, so there is no value to hand back (see CLAUDE.md, "Failure").
[[nodiscard]] inline bool try_spend_move_credit(int32_t& credits)
{
  if (credits <= 0)
    return false;
  --credits;
  return true;
}

} // namespace server
