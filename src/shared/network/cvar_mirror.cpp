#include "cvar_mirror.hpp"

#include "log.hpp"
#include "quantization.hpp"

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

} // namespace

void serialize_cvar_values(network::Bit_Writer&         writer,
                           const cvar_values_message_t& message)
{
  network::write_var_uint(writer,
                          static_cast<uint32_t>(message.values.size()));
  for (const cvar_value_t& value : message.values)
  {
    network::write_var_uint(writer, static_cast<uint32_t>(value.id));
    network::write_string(writer, value.text);
  }
}

cvar_values_message_t deserialize_cvar_values(network::Bit_Reader& reader)
{
  cvar_values_message_t message;
  uint32_t              count = network::read_var_uint(reader);
  message.values.resize(count);
  for (cvar_value_t& value : message.values)
  {
    value.id = static_cast<cvars::cvar_id>(network::read_var_uint(reader));
    network::read_string(reader, value.text);
  }
  return message;
}

cvar_values_message_t collect_mirrored_cvars(const cvars::cvar_state_t& state)
{
  cvar_values_message_t message;
  for (cvars::cvar_id id : cvars::mirrored_cvars())
  {
    // A formatting failure is a corrupted type tag, which try_cvar_to_text
    // treats as fatal -- so every mirrored cvar reaches the wire or nothing does.
    message.values.push_back({id, *cvars::try_cvar_to_text(state, id)});
  }
  return message;
}

cvar_values_message_t
collect_changed_mirrored_cvars(const cvars::cvar_state_t& current,
                               const cvars::cvar_state_t& last_broadcast)
{
  cvar_values_message_t message;
  for (cvars::cvar_id id : cvars::mirrored_cvars())
  {
    const cvars::cvar_info_t& info = cvars::cvar_info(id);
    if (std::memcmp(value_bytes(current, info),
                    value_bytes(last_broadcast, info), info.size) == 0)
      continue;

    message.values.push_back({id, *cvars::try_cvar_to_text(current, id)});
  }
  return message;
}

bool apply_cvar_values(cvars::cvar_state_t&         state,
                       const cvar_values_message_t& message)
{
  bool all_applied = true;
  for (const cvar_value_t& value : message.values)
  {
    // An id past the end of OUR table can only mean the two builds disagree
    // about cvars.def, which the SCHEMA_HASH handshake is supposed to have
    // refused already -- so it is a build problem, said loudly.
    if (static_cast<uint32_t>(value.id) >= cvars::CVAR_COUNT)
    {
      log_error("apply_cvar_values: cvar id {} is past the end of this build's "
                "table ({} cvars). The two builds disagree about cvars.def "
                "despite a matching schema hash.",
                static_cast<uint32_t>(value.id), cvars::CVAR_COUNT);
      all_applied = false;
      continue;
    }

    const cvars::cvar_info_t& info = cvars::cvar_info(value.id);

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

    if (!cvars::try_cvar_from_text(state, value.id, value.text))
    {
      log_error("apply_cvar_values: '{}' is not a valid value for '{}'; left "
                "unchanged",
                value.text, info.name);
      all_applied = false;
      continue;
    }
  }
  return all_applied;
}

} // namespace shared
