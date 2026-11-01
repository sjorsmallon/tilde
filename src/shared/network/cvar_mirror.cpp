#include "cvar_mirror.hpp"

#include "log.hpp"

#include <cstring>

namespace shared
{

namespace
{

// Where this cvar's value lives inside cvar_state_t. offset/size come from the
// generated table, which is what lets one memcmp serve every type without a
// switch per cvar.
const std::byte* value_bytes(const cvars::cvar_state_t& state,
                             const cvars::cvar_info_t&  info)
{
  return reinterpret_cast<const std::byte*>(&state) + info.offset;
}

std::byte* mutable_value_bytes(cvars::cvar_state_t&      state,
                               const cvars::cvar_info_t& info)
{
  return reinterpret_cast<std::byte*>(&state) + info.offset;
}

// A formatting failure is a corrupted type tag, which try_cvar_to_text treats
// as fatal -- so every mirrored cvar reaches the wire or nothing does. A value
// past the row's text bound is the same kind of impossibility: the longest
// mirrored cvar is a string<64>.
Cvar_Value cvar_value_from_state(const cvars::cvar_state_t& state, cvars::cvar_id id)
{
  Cvar_Value value;
  value.id = static_cast<uint16_t>(id);
  if (!value.text.set(cvars::try_cvar_to_text(state, id)->c_str()))
    fatal_error("cvar '{}' formats longer than a Cvar_Value's text holds", cvars::cvar_info(id).name);
  return value;
}

} // namespace

void write_cvar_value_messages(reliable_stream_t& stream, Span<const Cvar_Value> values)
{
  Cvar_Values message;
  for (const Cvar_Value& value : values)
  {
    if (message.values.full())
    {
      write_cvar_values(stream, message);
      message.values.clear();
    }
    (void)message.values.try_push_back(value);
  }
  if (!message.values.empty())
    write_cvar_values(stream, message);
}

std::vector<Cvar_Value> collect_mirrored_cvars(const cvars::cvar_state_t& state)
{
  std::vector<Cvar_Value> values;
  for (cvars::cvar_id id : cvars::mirrored_cvars())
    values.push_back(cvar_value_from_state(state, id));
  return values;
}

std::vector<Cvar_Value> collect_changed_mirrored_cvars(const cvars::cvar_state_t& current,
                                                       const cvars::cvar_state_t& last_broadcast)
{
  std::vector<Cvar_Value> values;
  for (cvars::cvar_id id : cvars::mirrored_cvars())
  {
    const cvars::cvar_info_t& info = cvars::cvar_info(id);
    if (std::memcmp(value_bytes(current, info),
                    value_bytes(last_broadcast, info), info.size) == 0)
      continue;

    values.push_back(cvar_value_from_state(current, id));
  }
  return values;
}

bool apply_cvar_values(cvars::cvar_state_t& state, Span<const Cvar_Value> values)
{
  bool all_applied = true;
  for (const Cvar_Value& value : values)
  {
    // An id past the end of OUR table can only mean the two builds disagree
    // about cvars.def, which the SCHEMA_HASH handshake is supposed to have
    // refused already -- so it is a build problem, said loudly.
    if (value.id >= cvars::CVAR_COUNT)
    {
      log_error("apply_cvar_values: cvar id {} is past the end of this build's "
                "table ({} cvars). The two builds disagree about cvars.def "
                "despite a matching schema hash.",
                value.id, cvars::CVAR_COUNT);
      all_applied = false;
      continue;
    }

    const cvars::cvar_id      id   = static_cast<cvars::cvar_id>(value.id);
    const cvars::cvar_info_t& info = cvars::cvar_info(id);

    // The server may only push what it OWNS. Anything else would silently
    // overwrite a value this process is the authority on.
    if ((info.flags & cvars::CVAR_FLAG_MIRRORED) == 0)
    {
      log_error("apply_cvar_values: server sent a value for '{}', which is not "
                "@Mirrored -- refusing to overwrite a locally-owned cvar",
                info.name);
      all_applied = false;
      continue;
    }

    if (!cvars::try_cvar_from_text(state, id, value.text.c_str()))
    {
      log_error("apply_cvar_values: '{}' is not a valid value for '{}'; left "
                "unchanged",
                value.text.c_str(), info.name);
      all_applied = false;
      continue;
    }
  }
  return all_applied;
}

void copy_cvars_from(cvars::cvar_state_t& state, const cvars::cvar_state_t& source,
                     Span<const cvars::cvar_id> ids)
{
  // Byte-copied through the same offset/size pair collect_changed compares
  // through, so the two cannot disagree about what a value IS.
  for (cvars::cvar_id id : ids)
  {
    const cvars::cvar_info_t& info = cvars::cvar_info(id);
    std::memcpy(mutable_value_bytes(state, info), value_bytes(source, info), info.size);
  }
}

void revert_cvars_to_defaults(cvars::cvar_state_t& state, Span<const cvars::cvar_id> ids)
{
  // cvar_state_t's member initializers ARE the cvars.def defaults, so a
  // default-constructed one is the authority on what "nobody has told us"
  // means. Static because it is immutable and identical on every call.
  static const cvars::cvar_state_t defaults{};
  copy_cvars_from(state, defaults, ids);
}

void revert_mirrored_cvars_to_defaults(cvars::cvar_state_t& state)
{
  revert_cvars_to_defaults(state, cvars::mirrored_cvars());
}

} // namespace shared
