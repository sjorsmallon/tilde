#include "surface_kind.hpp"

namespace shared
{

std::optional<Surface_Kind> try_parse_surface_kind(std::string_view text)
{
  const auto is_space = [](char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
  };
  while (!text.empty() && is_space(text.front()))
    text.remove_prefix(1);
  while (!text.empty() && is_space(text.back()))
    text.remove_suffix(1);

  for (const surface_kind_name_t& row : SURFACE_KIND_NAMES)
    if (text == row.name)
      return row.kind;
  return std::nullopt;
}

} // namespace shared
