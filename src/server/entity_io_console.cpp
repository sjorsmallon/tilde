#include "entity_io_console.hpp"

#include "../shared/reflection.hpp"

#include <cctype>
#include <format>

namespace server
{

namespace
{

std::string_view trimmed(std::string_view text)
{
  while (!text.empty() && std::isspace((unsigned char)text.front()))
    text.remove_prefix(1);
  while (!text.empty() && std::isspace((unsigned char)text.back()))
    text.remove_suffix(1);
  return text;
}

// Where one pair starts. `name_begin` is where the NAME starts, which is what
// ends the previous pair's value -- collected for the whole input before any
// value is cut, because a value runs to the next name and not to the next space.
struct pair_t
{
  const field_info_t* field       = nullptr;
  size_t              name_begin  = 0;
  size_t              value_begin = 0;
};

} // namespace

std::vector<std::string> parse_action_parameters(entities::action_data_t& data,
                                                 std::string_view         parameters)
{
  std::vector<std::string> refusals;

  const Span<const field_info_t> fields  = entities::action_payload_fields(data.tag);
  uint8_t*                       payload = entities::action_payload_bytes(data);

  std::vector<pair_t> pairs;
  for (size_t at = 0; at < parameters.size(); ++at)
  {
    if (parameters[at] != '=')
      continue;

    size_t name_begin = at;
    while (name_begin > 0 && !std::isspace((unsigned char)parameters[name_begin - 1]))
      --name_begin;

    const std::string_view candidate = parameters.substr(name_begin, at - name_begin);
    for (const field_info_t& field : fields)
    {
      if (candidate != field.name)
        continue;

      pairs.push_back({&field, name_begin, at + 1});
      break;
    }
  }

  // Text before the first pair is something the author typed that nothing will
  // read -- reported rather than dropped.
  const std::string_view leading =
      trimmed(parameters.substr(0, pairs.empty() ? parameters.size() : pairs.front().name_begin));
  if (!leading.empty())
    refusals.push_back(std::format("'{}' is not a field=value pair for {}", leading,
                                   entities::to_string(data.tag)));

  for (size_t which = 0; which < pairs.size(); ++which)
  {
    const size_t value_end =
        which + 1 < pairs.size() ? pairs[which + 1].name_begin : parameters.size();
    const std::string value(
        trimmed(parameters.substr(pairs[which].value_begin, value_end - pairs[which].value_begin)));

    if (!field_from_text(value, *pairs[which].field, payload + pairs[which].field->offset))
      refusals.push_back(std::format("{}=\"{}\" could not be read — the parameter keeps its default",
                                     pairs[which].field->name, value));
  }

  return refusals;
}

} // namespace server
