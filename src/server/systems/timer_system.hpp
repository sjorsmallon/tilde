#pragma once

namespace server
{

struct server_context_t;

// Every running Timer whose deadline has come emits Elapsed, in the same
// statement that stops it or, for a repeating one, rearms it. The timer says
// WHEN; what elapsing does is a connection.
//
// Runs just before the drain, so a chain hanging off Elapsed settles inside the
// tick the timer ran out in.
void update_timers(server_context_t& context);

} // namespace server
