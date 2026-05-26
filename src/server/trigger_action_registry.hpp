#pragma once

// =============================================================================
// Trigger Action Registry (server-side)
// =============================================================================
// Named-action dispatch table for Trigger_Volume_Entity. Each action is a
// plain C function registered at static-init time with a string name. The
// trigger entity stores the chosen name in its `action_name` field; the
// server looks it up at fire time and invokes it.
//
// Design contract:
//   - Action lookup is by STRING at fire time, not by ID at registration time.
//     Reordering or removing actions therefore does not silently rebind any
//     saved trigger in any map — the name IS the identity.
//   - Registration order is undefined across translation units; this is OK
//     because lookups happen at fire time, well after all static init has run.
//   - Unknown action names produce a `log_error` on the server. We do not
//     silently drop the trigger (per the "no silent failures" project rule).
//   - Action functions receive the full server_context_t, the trigger, and
//     the player. The context exposes the game session, physics, the event
//     queue, and the damage helper — everything an action might plausibly
//     need. Configuration lives on the trigger's typed parameter slots:
//        trigger.param_target_name  (pascal_string)
//        trigger.param_string       (pascal_string)
//        trigger.param_float        (float32)
//     Each action reads only the slots it cares about; unused slots are
//     ignored.
//
// Lives in `src/server/`, not `src/shared/`, because actions now legitimately
// need server-side state (physics, inflict_damage, event queue). The client
// editor's "what actions exist?" question is answered separately by
// `src/shared/trigger_action_list.hpp` — a names-only X-macro both sides
// include. That split kills two birds: the server-DLL singleton holds the
// real function pointers, and the client never sees server-only types in
// the registry's signatures.
//
// No `force_link_*` shim is needed here because game_server is a SHARED
// library — its static initializers are not subject to the static-lib
// archive-member drop that motivated the shim in the old game_shared
// version. (See memory/project_static_init_dropped_from_static_lib.md.)
// =============================================================================

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace network
{
class Trigger_Volume_Entity;
class Player_Entity;
}

namespace server
{

struct server_context_t;

using trigger_action_fn = void (*)(server_context_t &context,
                                   network::Trigger_Volume_Entity &trigger,
                                   network::Player_Entity &player);

class Trigger_Action_Registry
{
public:
  static Trigger_Action_Registry &get();

  void register_action(const std::string &name, trigger_action_fn fn);

  // Returns nullptr if no action is registered under this name.
  trigger_action_fn find_action(const std::string &name) const;

  // Returns names sorted lexicographically so iteration order is stable.
  // Used at startup-time validation (every name in TRIGGER_ACTION_LIST must
  // resolve to a registered handler), not for editor UI — the editor reads
  // names directly from the X-macro in src/shared/trigger_action_list.hpp.
  std::vector<std::string> list_action_names() const;

private:
  std::unordered_map<std::string, trigger_action_fn> registry_;
};

// Static-init self-registration helper. Declare a `static` instance per
// built-in action; the constructor registers it before main().
struct Trigger_Action_Registration
{
  Trigger_Action_Registration(const std::string &name, trigger_action_fn fn);
};

} // namespace server
