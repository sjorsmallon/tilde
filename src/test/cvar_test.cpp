// cvar_test -- the generated cvar/command tables, the console dispatcher, the
// argument binders, and the @Mirrored wire path.
//
// The four things worth pinning down, in the order they appear below:
//
//   1. TEXT CONVERSION is a closed set BOTH WAYS. The old CVar<T> mapped any
//      bool text outside 1/true/yes/on to FALSE, so `debug_show_navmesh tru`
//      silently turned it off, and `pm_maxspeed 320abc` read as 320 because
//      stringstream stops at the first bad char. Both are now rejections that
//      leave the value alone.
//   2. OWNERSHIP is decided by the declared flags plus whether
//      forward_to_server is installed -- not by a build flag. Same line, same
//      dispatcher, different side.
//   3. The ARGUMENT BINDERS are generated from the declared signature, so the
//      usage string cannot drift from what is actually accepted. This file
//      compiles the two generated binder TUs and supplies its own recording
//      handlers, which is also a standing check that the binder TU references
//      nothing but `commands::<name>`.
//   4. MIRRORING detects changes by COMPARING against a retained copy, so a
//      direct field write replicates -- there is no Set() to forget.
//
// What this cannot reach is the server_impl / play_state wiring (who sends
// when); that is exercised by running MyGame_Client against MyGame_Server.

#include "cvars/cvar_console.hpp"
#include "cvars/generated/cvars_generated.hpp"
#include "network/bitstream.hpp"
#include "network/cvar_mirror.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int failure_count = 0;

void check(bool condition, const char* what)
{
  if (!condition)
  {
    std::cout << "  FAIL: " << what << "\n";
    ++failure_count;
  }
}

void check_equal(const std::string& actual, const std::string& expected,
                 const char* what)
{
  if (actual != expected)
  {
    std::cout << "  FAIL: " << what << " -- expected '" << expected
              << "', got '" << actual << "'\n";
    ++failure_count;
  }
}

// --- Recorded command invocations -------------------------------------------
//
// The generated binder TUs reference commands::<name> directly, so defining
// them here is what lets this test link at all -- the same link-time obligation
// the real client and server DLLs carry.

struct call_record_t
{
  int              count = 0;
  int              caller_slot = -99;
  cvars::Bot_Mode  bot_mode = cvars::Bot_Mode::regular;
  bool             flag = false;
  std::string      first_string;
  std::string      rest_string;
};

call_record_t g_spawn_bot;
call_record_t g_spawn_cube;
call_record_t g_spawn_sphere;
call_record_t g_map;
call_record_t g_noclip;
call_record_t g_bind;

void reset_records()
{
  g_spawn_bot = g_spawn_cube = g_spawn_sphere = {};
  g_map = g_noclip = g_bind = {};
}

// The line buffer a console command's argument views point into must outlive
// the call, and a `string...` rest parameter is only recoverable while it does.
// execute_console_line takes a string_view, so the caller owns the storage.
cvars::console_result_t run(cvars::cvar_state_t&   state,
                            cvars::command_table_t& table,
                            const std::string&      line, std::string* out_reply,
                            int caller_slot = -1)
{
  cvars::command_context_t context{.caller_slot = caller_slot};
  return cvars::execute_console_line(state, table, line, context, out_reply);
}

std::string g_forwarded_line;
int         g_forward_count = 0;

void record_forward(std::string_view line)
{
  g_forwarded_line = std::string(line);
  ++g_forward_count;
}

} // namespace

namespace cvars::commands
{

void spawn_bot(Bot_Mode mode, const command_context_t& context)
{
  ++g_spawn_bot.count;
  g_spawn_bot.bot_mode    = mode;
  g_spawn_bot.caller_slot = context.caller_slot;
}

void spawn_cube(const command_context_t& context)
{
  ++g_spawn_cube.count;
  g_spawn_cube.caller_slot = context.caller_slot;
}

void spawn_sphere(const command_context_t& context)
{
  ++g_spawn_sphere.count;
  g_spawn_sphere.caller_slot = context.caller_slot;
}

void map(std::string_view path, const command_context_t& context)
{
  ++g_map.count;
  g_map.first_string = std::string(path);
  g_map.caller_slot  = context.caller_slot;
}

void noclip(bool enabled, const command_context_t& context)
{
  ++g_noclip.count;
  g_noclip.flag        = enabled;
  g_noclip.caller_slot = context.caller_slot;
}

void bind(std::string_view key, std::string_view command,
          const command_context_t& context)
{
  ++g_bind.count;
  g_bind.first_string = std::string(key);
  g_bind.rest_string  = std::string(command);
  g_bind.caller_slot  = context.caller_slot;
}

} // namespace cvars::commands

namespace
{

// --- 1. The generated tables ------------------------------------------------

void test_tables()
{
  std::cout << "[tables]\n";

  check(cvars::cvar_infos().size() == cvars::CVAR_COUNT,
        "cvar_infos() spans CVAR_COUNT entries");
  check(cvars::command_infos().size() == cvars::COMMAND_COUNT,
        "command_infos() spans COMMAND_COUNT entries");

  // Every entry is described. A missing description means the .def grammar's
  // mandatory description literal stopped being mandatory.
  for (uint32_t index = 0; index < cvars::CVAR_COUNT; ++index)
  {
    const cvars::cvar_info_t& info = cvars::cvar_info((cvars::cvar_id)index);
    check(info.name != nullptr && info.name[0] != '\0', "cvar has a name");
    check(info.description != nullptr && info.description[0] != '\0',
          "cvar has a description");
    check(info.size > 0, "cvar value has a nonzero size");
    check(info.offset + info.size <= sizeof(cvars::cvar_state_t),
          "cvar value lies inside cvar_state_t");
  }

  for (uint32_t index = 0; index < cvars::COMMAND_COUNT; ++index)
  {
    const cvars::command_info_t& info =
        cvars::command_info((cvars::command_id)index);
    check(info.usage != nullptr && info.usage[0] != '\0',
          "command has a usage string");
    // "A command must declare @Client or @Server, because that is which binder
    // TU references its handler."
    check((info.flags & (cvars::CVAR_FLAG_CLIENT | cvars::CVAR_FLAG_SERVER)) != 0,
          "command declares a side");
  }

  // Cvars and commands share ONE flat namespace -- the console resolves both
  // from the same token, so a name may not resolve to both.
  for (uint32_t index = 0; index < cvars::CVAR_COUNT; ++index)
  {
    cvars::command_id collision{};
    check(!cvars::find_command(cvars::cvar_info((cvars::cvar_id)index).name,
                               &collision),
          "no cvar name is also a command name");
  }

  cvars::cvar_id    id{};
  cvars::command_id command{};
  check(cvars::find_cvar("pm_maxspeed", &id) && id == cvars::cvar_id::pm_maxspeed,
        "find_cvar resolves pm_maxspeed");
  check(cvars::find_command("spawn_bot", &command) &&
            command == cvars::command_id::spawn_bot,
        "find_command resolves spawn_bot");
  check(!cvars::find_cvar("pm_maxspeedd", &id), "find_cvar rejects a near miss");
  check(!cvars::find_command("", &command), "find_command rejects an empty name");

  // The @Mirrored subset is published so both ends agree on the sync set by
  // construction rather than by each filtering on flags and hoping.
  uint32_t mirrored_by_flag = 0;
  for (uint32_t index = 0; index < cvars::CVAR_COUNT; ++index)
    if (cvars::cvar_info((cvars::cvar_id)index).flags & cvars::CVAR_FLAG_MIRRORED)
      ++mirrored_by_flag;
  check(cvars::mirrored_cvars().size() == mirrored_by_flag,
        "mirrored_cvars() holds exactly the @Mirrored-flagged cvars");
  for (cvars::cvar_id mirrored : cvars::mirrored_cvars())
    check((cvars::cvar_info(mirrored).flags & cvars::CVAR_FLAG_MIRRORED) != 0,
          "every mirrored_cvars() entry is flagged @Mirrored");

  // @Client and @Server are ownership claims, so they are mutually exclusive.
  for (uint32_t index = 0; index < cvars::CVAR_COUNT; ++index)
  {
    uint32_t flags = cvars::cvar_info((cvars::cvar_id)index).flags;
    check((flags & (cvars::CVAR_FLAG_CLIENT | cvars::CVAR_FLAG_SERVER |
                    cvars::CVAR_FLAG_MIRRORED)) !=
              (cvars::CVAR_FLAG_CLIENT | cvars::CVAR_FLAG_SERVER),
          "no cvar claims both @Client and @Server");
  }
}

// --- 2. Text conversion -----------------------------------------------------

void test_text_conversion()
{
  std::cout << "[text conversion]\n";

  cvars::cvar_state_t state;
  std::string         text;

  check(cvars::cvar_to_text(state, cvars::cvar_id::pm_maxspeed, text),
        "cvar_to_text succeeds for an f32");
  check_equal(text, "320", "f32 default formats without a trailing .0");

  check(cvars::cvar_to_text(state, cvars::cvar_id::debug_show_navmesh, text),
        "cvar_to_text succeeds for a bool");
  check_equal(text, "0", "false formats as 0");

  // Floats use the shortest representation that ROUND-TRIPS -- the value that
  // comes back must be bit-identical, or a mirrored pm_* value would drift the
  // client's prediction away from the server's simulation one sync at a time.
  const float probes[] = {1.001f, 0.1f, 123.456f, -0.0f, 800.0f, 1e-7f, 3.4e38f};
  for (float probe : probes)
  {
    state.pm_overbounce = probe;
    check(cvars::cvar_to_text(state, cvars::cvar_id::pm_overbounce, text),
          "cvar_to_text succeeds");
    cvars::cvar_state_t destination;
    check(cvars::cvar_from_text(destination, cvars::cvar_id::pm_overbounce, text),
          "cvar_from_text accepts what cvar_to_text produced");
    check(std::memcmp(&destination.pm_overbounce, &probe, sizeof(float)) == 0,
          "float round-trips bit-exactly through text");
  }

  // A partial numeric parse is a REJECTION, not a truncation. stringstream
  // would have read this as 320.
  state.pm_maxspeed = 320.f;
  check(!cvars::cvar_from_text(state, cvars::cvar_id::pm_maxspeed, "320abc"),
        "trailing garbage rejects the whole token");
  check(state.pm_maxspeed == 320.f, "a rejected numeric parse leaves the value alone");
  check(!cvars::cvar_from_text(state, cvars::cvar_id::pm_maxspeed, ""),
        "empty text is rejected");
  check(!cvars::cvar_from_text(state, cvars::cvar_id::pm_maxspeed, "  "),
        "whitespace is rejected");
  check(cvars::cvar_from_text(state, cvars::cvar_id::pm_maxspeed, "-12.5"),
        "a negative float parses");
  check(state.pm_maxspeed == -12.5f, "the negative float landed");

  // Bools are a closed set BOTH ways. 'tru' used to mean false.
  const char* truthy[] = {"1", "true", "yes", "on"};
  const char* falsy[]  = {"0", "false", "no", "off"};
  for (const char* token : truthy)
  {
    state.debug_show_navmesh = false;
    check(cvars::cvar_from_text(state, cvars::cvar_id::debug_show_navmesh, token),
          "a truthy token parses");
    check(state.debug_show_navmesh, "a truthy token sets true");
  }
  for (const char* token : falsy)
  {
    state.debug_show_navmesh = true;
    check(cvars::cvar_from_text(state, cvars::cvar_id::debug_show_navmesh, token),
          "a falsy token parses");
    check(!state.debug_show_navmesh, "a falsy token sets false");
  }
  state.debug_show_navmesh = true;
  check(!cvars::cvar_from_text(state, cvars::cvar_id::debug_show_navmesh, "tru"),
        "an unrecognised bool token is rejected");
  check(state.debug_show_navmesh,
        "a rejected bool leaves the value alone -- it does not fall back to false");
}

// --- 3. The console dispatcher ----------------------------------------------

void test_console_cvars()
{
  std::cout << "[console: cvars]\n";

  cvars::cvar_state_t    state;
  cvars::command_table_t table; // no binders, no forwarder: a lone process
  std::string            reply;

  check(run(state, table, "", &reply) == cvars::console_result_t::empty,
        "an empty line is empty, not unknown");
  check(run(state, table, "   \t ", &reply) == cvars::console_result_t::empty,
        "a whitespace-only line is empty");
  check(run(state, table, "no_such_thing", &reply) ==
            cvars::console_result_t::unknown_name,
        "an unknown name is reported");

  // A bare read is always local, even for a cvar this process does not own.
  check(run(state, table, "pm_maxspeed", &reply) == cvars::console_result_t::ok,
        "a bare read succeeds");
  check(reply.find("pm_maxspeed is 320") != std::string::npos,
        "the read reply quotes the value");
  check(reply.find("[MIRRORED]") != std::string::npos,
        "the read reply quotes the ownership flags");
  check(reply.find("Maximum player speed") != std::string::npos,
        "the read reply quotes the description");

  check(run(state, table, "cl_timescale 0.5", &reply) ==
            cvars::console_result_t::ok,
        "a local write succeeds");
  check(state.cl_timescale == 0.5f, "the local write landed");

  check(run(state, table, "cl_timescale nonsense", &reply) ==
            cvars::console_result_t::bad_arguments,
        "an unparseable value is bad_arguments");
  check(state.cl_timescale == 0.5f,
        "a rejected write leaves the previous value live");
  check(reply.find("unchanged") != std::string::npos,
        "the rejection says the value is unchanged rather than letting the "
        "user assume the set landed");

  check(run(state, table, "cl_timescale 1 2", &reply) ==
            cvars::console_result_t::bad_arguments,
        "a non-string cvar takes exactly one value");
  check(state.cl_timescale == 0.5f, "the over-long write landed nothing");

  // Not connected: a @Server/@Mirrored write with no forwarder runs LOCALLY.
  // That is the dedicated server and the single-player case, and it is why
  // ownership is decided by forward_to_server rather than by a build flag.
  check(run(state, table, "pm_maxspeed 500", &reply) == cvars::console_result_t::ok,
        "a mirrored write with no forwarder runs locally");
  check(state.pm_maxspeed == 500.f, "the local mirrored write landed");
}

void test_console_forwarding()
{
  std::cout << "[console: forwarding]\n";

  cvars::cvar_state_t    state;
  cvars::command_table_t table;
  std::string            reply;

  g_forwarded_line.clear();
  g_forward_count = 0;
  table.forward_to_server = &record_forward;

  // A networked client does not own server state: the whole LINE goes upstream
  // and comes back through this same function on the other side.
  check(run(state, table, "pm_maxspeed 500", &reply) ==
            cvars::console_result_t::forwarded,
        "a @Mirrored write forwards");
  check_equal(g_forwarded_line, "pm_maxspeed 500", "the whole line forwards");
  check(state.pm_maxspeed == 320.f,
        "a forwarded write does NOT also apply locally -- the mirror comes back "
        "over the wire");

  check(run(state, table, "sv_tickrate 128", &reply) ==
            cvars::console_result_t::forwarded,
        "a @Server write forwards");

  check(run(state, table, "spawn_bot chase", &reply) ==
            cvars::console_result_t::forwarded,
        "a @Server command forwards");
  check_equal(g_forwarded_line, "spawn_bot chase",
              "the command line forwards untokenized");
  check(g_spawn_bot.count == 0, "a forwarded command runs nothing locally");

  // A READ is still local: both sides compile the same table, so printing the
  // local value costs no round trip. Only a WRITE has to respect ownership.
  g_forward_count = 0;
  check(run(state, table, "pm_maxspeed", &reply) == cvars::console_result_t::ok,
        "a bare read of a server-owned cvar stays local");
  check(g_forward_count == 0, "the bare read forwarded nothing");

  // @Client and unflagged names are ours even while connected.
  check(run(state, table, "cl_timescale 2", &reply) == cvars::console_result_t::ok,
        "an unflagged write stays local while connected");
  check(state.cl_timescale == 2.f, "the unflagged write landed locally");
  check(run(state, table, "r_fov 110", &reply) == cvars::console_result_t::ok,
        "a @Client write stays local while connected");
  check(state.r_fov == 110.f, "the @Client write landed locally");
}

// A line that ARRIVED FROM THE WIRE carries a real caller_slot, has already
// been forwarded once, and must never be forwarded again. Without this, a
// process holding both a forwarder and the authority bounces the line back to
// itself forever -- which is exactly what the integrated build did when its
// client and server shared one command_table_t: `spawn_bot` ping-ponged over
// loopback and the handler never ran.
void test_no_forward_loop()
{
  std::cout << "[console: no forward loop]\n";

  cvars::cvar_state_t    state;
  cvars::command_table_t table;
  cvars::bind_server_commands(table);
  std::string reply;

  // The pathological setup: a forwarder installed on a table that ALSO owns
  // the @Server handlers.
  g_forwarded_line.clear();
  g_forward_count         = 0;
  table.forward_to_server = &record_forward;

  reset_records();
  check(run(state, table, "spawn_bot chase", &reply, /*caller_slot=*/0) ==
            cvars::console_result_t::ok,
        "a remote @Server command RUNS instead of bouncing");
  check(g_forward_count == 0, "a remote line is never forwarded again");
  check(g_spawn_bot.count == 1, "the handler actually ran");
  check(g_spawn_bot.bot_mode == cvars::Bot_Mode::chase,
        "the remote line's arguments bound normally");

  check(run(state, table, "pm_maxspeed 640", &reply, /*caller_slot=*/2) ==
            cvars::console_result_t::ok,
        "a remote @Mirrored write applies instead of bouncing");
  check(g_forward_count == 0, "the remote cvar write was not forwarded");
  check(state.pm_maxspeed == 640.f,
        "the remote write landed, so the mirror has something to broadcast");

  // A LOCAL line on that same table still forwards -- the distinction is the
  // caller, not the table.
  reset_records();
  check(run(state, table, "spawn_bot idle", &reply, /*caller_slot=*/-1) ==
            cvars::console_result_t::forwarded,
        "a local line on the same table still forwards");
  check(g_forward_count == 1, "the local line went upstream");
  check(g_spawn_bot.count == 0, "the forwarded local line ran nothing locally");
}

void test_console_missing_handlers()
{
  std::cout << "[console: unbound commands]\n";

  cvars::cvar_state_t    state;
  cvars::command_table_t table; // nothing bound, nothing forwarded
  std::string            reply;

  // A @Server command on a disconnected client: not an internal error, and
  // reachable from a REMOTE line, so it must not assert.
  check(run(state, table, "spawn_bot", &reply) ==
            cvars::console_result_t::not_connected,
        "a @Server command with no handler and no forwarder is not_connected");
  check(reply.find("not") != std::string::npos, "the reply explains why");

  // A @Client command reaching a dedicated server, which never called
  // bind_client_commands.
  check(run(state, table, "bind x noclip", &reply) ==
            cvars::console_result_t::no_handler,
        "a @Client command with no handler is no_handler");
}

// --- 4. The generated argument binders --------------------------------------

void test_command_binders()
{
  std::cout << "[argument binders]\n";

  cvars::cvar_state_t    state;
  cvars::command_table_t table;
  cvars::bind_server_commands(table);
  cvars::bind_client_commands(table);
  std::string reply;

  for (uint32_t index = 0; index < cvars::COMMAND_COUNT; ++index)
    check(table.binders[index] != nullptr,
          "both binder TUs together fill every slot");

  // Defaults: an omitted optional parameter takes its declared value.
  reset_records();
  check(run(state, table, "spawn_bot", &reply, 3) == cvars::console_result_t::ok,
        "spawn_bot with no argument succeeds");
  check(g_spawn_bot.count == 1, "spawn_bot ran once");
  check(g_spawn_bot.bot_mode == cvars::Bot_Mode::idle,
        "the omitted mode took its declared default");
  check(g_spawn_bot.caller_slot == 3,
        "the context reaches the handler unchanged");

  // Enum values bind BY NAME, lowercase, the way they are typed.
  reset_records();
  check(run(state, table, "spawn_bot chase", &reply) == cvars::console_result_t::ok,
        "spawn_bot chase succeeds");
  check(g_spawn_bot.bot_mode == cvars::Bot_Mode::chase, "the enum bound by name");

  reset_records();
  check(run(state, table, "spawn_bot Chase", &reply) ==
            cvars::console_result_t::bad_arguments,
        "enum names are case-sensitive, matching how the console resolves");
  check(g_spawn_bot.count == 0, "a rejected argument calls nothing");
  check(reply.find("usage:") != std::string::npos,
        "the failure replies with the generated usage string");
  check(reply.find("idle|chase|regular") != std::string::npos,
        "the usage string enumerates the accepted enum values, so it cannot "
        "drift from what the binder accepts");

  reset_records();
  check(run(state, table, "spawn_bot idle extra", &reply) ==
            cvars::console_result_t::bad_arguments,
        "too many arguments is a usage error");
  check(g_spawn_bot.count == 0, "the over-long call ran nothing");

  // A no-argument command rejects arguments rather than ignoring them.
  reset_records();
  check(run(state, table, "spawn_cube", &reply) == cvars::console_result_t::ok,
        "spawn_cube succeeds");
  check(g_spawn_cube.count == 1, "spawn_cube ran once");
  check(run(state, table, "spawn_sphere here", &reply) ==
            cvars::console_result_t::bad_arguments,
        "a no-argument command rejects an argument");
  check(g_spawn_sphere.count == 0, "the rejected call ran nothing");

  // A required parameter is required.
  reset_records();
  check(run(state, table, "map", &reply) == cvars::console_result_t::bad_arguments,
        "a required parameter cannot be omitted");
  check(g_map.count == 0, "the incomplete call ran nothing");
  check(run(state, table, "map dm_aabb.source", &reply) ==
            cvars::console_result_t::ok,
        "map with a path succeeds");
  check_equal(g_map.first_string, "dm_aabb.source", "the path bound");

  // Bool parameters use the same closed set as a bool cvar write.
  reset_records();
  check(run(state, table, "noclip", &reply) == cvars::console_result_t::ok,
        "noclip with no argument takes its default");
  check(g_noclip.flag == false, "the default is false");
  check(run(state, table, "noclip on", &reply) == cvars::console_result_t::ok,
        "noclip on succeeds");
  check(g_noclip.flag == true, "'on' bound as true");
  reset_records();
  check(run(state, table, "noclip tru", &reply) ==
            cvars::console_result_t::bad_arguments,
        "an unrecognised bool argument is rejected, not read as false");
  check(g_noclip.count == 0, "the rejected bool ran nothing");

  // `string...` takes the line's untokenized tail, interior spacing intact --
  // this is the whole reason the arg views point into one contiguous buffer.
  reset_records();
  std::string bind_line = "bind k say hello   world";
  check(run(state, table, bind_line, &reply) == cvars::console_result_t::ok,
        "bind with a rest parameter succeeds");
  check_equal(g_bind.first_string, "k", "the key bound");
  check_equal(g_bind.rest_string, "say hello   world",
              "the rest parameter kept the line's interior whitespace");

  reset_records();
  check(run(state, table, "bind k", &reply) == cvars::console_result_t::bad_arguments,
        "a rest parameter still needs at least one token");
  check(g_bind.count == 0, "the incomplete bind ran nothing");
}

// --- 5. Mirroring -----------------------------------------------------------

void test_mirroring()
{
  std::cout << "[mirroring]\n";

  cvars::cvar_state_t server;
  cvars::cvar_state_t last_broadcast = server;

  // Nothing changed yet: no traffic. A per-tick diff that always fires would
  // put ten pairs on the wire every tick forever.
  check(shared::collect_changed_mirrored_cvars(server, last_broadcast)
            .values.empty(),
        "an unchanged state produces no pairs");

  // A DIRECT field write replicates -- there is no Set() to forget to call.
  server.pm_maxspeed = 400.f;
  shared::cvar_values_message_t changed =
      shared::collect_changed_mirrored_cvars(server, last_broadcast);
  check(changed.values.size() == 1, "exactly the changed cvar is collected");
  check(changed.values[0].id == cvars::cvar_id::pm_maxspeed,
        "the changed cvar is the one that changed");
  check_equal(changed.values[0].text, "400", "the pair carries the new value");

  // A non-@Mirrored change is NOT collected: the client owns its own copy.
  last_broadcast          = server;
  server.debug_show_navmesh = true;
  server.cl_timescale       = 0.25f;
  check(shared::collect_changed_mirrored_cvars(server, last_broadcast)
            .values.empty(),
        "changes to unflagged cvars produce no pairs");

  // The full set is what a joining client gets, so it must be the whole
  // @Mirrored subset and nothing else.
  shared::cvar_values_message_t full = shared::collect_mirrored_cvars(server);
  check(full.values.size() == cvars::mirrored_cvars().size(),
        "the full set covers every @Mirrored cvar");
  for (const shared::cvar_value_t& value : full.values)
    check((cvars::cvar_info(value.id).flags & cvars::CVAR_FLAG_MIRRORED) != 0,
          "the full set carries only @Mirrored cvars");

  // Wire round-trip, then apply: a fresh client must end up bit-identical to
  // the server on every mirrored member, because it feeds them into the same
  // player_move() the server runs.
  server.pm_maxspeed  = 411.5f;
  server.pm_friction  = 6.25f;
  server.g_gravity    = 812.125f;
  server.pm_overbounce = 1.001f;
  full = shared::collect_mirrored_cvars(server);

  network::Bit_Writer writer;
  shared::serialize_cvar_values(writer, full);
  network::Bit_Reader reader(writer.buffer.data(), writer.buffer.size());
  shared::cvar_values_message_t received = shared::deserialize_cvar_values(reader);

  check(received.values.size() == full.values.size(),
        "the pair count survives the wire");
  for (size_t index = 0; index < received.values.size(); ++index)
  {
    check(received.values[index].id == full.values[index].id,
          "the cvar id survives the wire");
    check_equal(received.values[index].text, full.values[index].text,
                "the value text survives the wire");
  }

  cvars::cvar_state_t client;
  check(shared::apply_cvar_values(client, received),
        "a well-formed message applies cleanly");
  for (cvars::cvar_id id : cvars::mirrored_cvars())
  {
    const cvars::cvar_info_t& info = cvars::cvar_info(id);
    const std::byte* server_bytes =
        reinterpret_cast<const std::byte*>(&server) + info.offset;
    const std::byte* client_bytes =
        reinterpret_cast<const std::byte*>(&client) + info.offset;
    check(std::memcmp(server_bytes, client_bytes, info.size) == 0,
          "the client's mirrored value is bit-identical to the server's");
  }

  // Applying must not touch anything the server does not own.
  check(client.cl_timescale == 1.0f,
        "an unflagged cvar is untouched by a mirror apply");
  check(client.debug_show_navmesh == false,
        "a client-side debug toggle is untouched by a mirror apply");

  // The server may only push what it OWNS. A pair naming a non-@Mirrored cvar
  // is refused rather than silently overwriting a locally-owned value.
  std::cout << "  (the next few log_errors are expected -- rejection tests)\n";
  shared::cvar_values_message_t hostile;
  hostile.values.push_back({cvars::cvar_id::cl_timescale, "9"});
  cvars::cvar_state_t defended;
  check(!shared::apply_cvar_values(defended, hostile),
        "a non-@Mirrored pair is refused");
  check(defended.cl_timescale == 1.0f, "the refused pair changed nothing");

  // An id past the end of our table can only mean the two builds disagree
  // about cvars.def despite a matching schema hash -- loud, not silent.
  shared::cvar_values_message_t out_of_range;
  out_of_range.values.push_back({(cvars::cvar_id)cvars::CVAR_COUNT, "1"});
  check(!shared::apply_cvar_values(defended, out_of_range),
        "an out-of-range cvar id is refused");

  // One bad pair must not drop the rest: a tick's worth of movement constants
  // still lands, and only the bad one is reported.
  shared::cvar_values_message_t mixed;
  mixed.values.push_back({cvars::cvar_id::pm_maxspeed, "not_a_number"});
  mixed.values.push_back({cvars::cvar_id::pm_jumpspeed, "290"});
  cvars::cvar_state_t partial;
  check(!shared::apply_cvar_values(partial, mixed),
        "a message with a bad pair reports failure");
  check(partial.pm_maxspeed == 320.f, "the bad pair left its value alone");
  check(partial.pm_jumpspeed == 290.f, "the good pair still applied");

  // An empty message is legal and does nothing (the server simply never sends
  // one, but a truncated read must not be mistaken for a failure).
  check(shared::apply_cvar_values(partial, {}), "an empty message applies cleanly");
}

} // namespace

int main()
{
  std::cout << "=== cvar_test ===\n";

  test_tables();
  test_text_conversion();
  test_console_cvars();
  test_console_forwarding();
  test_no_forward_loop();
  test_console_missing_handlers();
  test_command_binders();
  test_mirroring();

  if (failure_count != 0)
  {
    std::cout << "\ncvar_test FAILED with " << failure_count << " failure(s)\n";
    return 1;
  }
  std::cout << "\ncvar_test: all checks passed\n";
  return 0;
}
