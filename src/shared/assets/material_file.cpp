#include "material_file.hpp"

#include "log.hpp"

#include <string>

namespace assets
{

namespace
{

bool is_space(char character)
{
  return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

std::string_view trim(std::string_view text)
{
  while (!text.empty() && is_space(text.front()))
    text.remove_prefix(1);
  while (!text.empty() && is_space(text.back()))
    text.remove_suffix(1);
  return text;
}

} // namespace

std::optional<material_shader_t> try_parse_material_shader(std::string_view word)
{
  word = trim(word);
  for (const material_shader_name_t& row : MATERIAL_SHADER_NAMES)
    if (word == row.name)
      return row.shader;
  return std::nullopt;
}

material_file_t parse_material_file(std::string_view text, std::string_view folder)
{
  material_file_t file;
  while (!text.empty())
  {
    const size_t           newline = text.find('\n');
    const std::string_view line    = trim(text.substr(0, newline));
    text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
    if (line.empty())
      continue;

    const size_t           space = line.find_first_of(" \t");
    const std::string_view key   = line.substr(0, space);
    const std::string_view value = space == std::string_view::npos ? std::string_view{} : trim(line.substr(space));
    if (value.empty())
      fatal_error("material '{}': material.txt line '{}' has a key and no value", folder, line);

    if (key == "shader")
    {
      const std::optional<material_shader_t> shader = try_parse_material_shader(value);
      if (!shader)
        fatal_error("material '{}': material.txt says shader '{}', which names no material_shader_t "
                    "(material_file.hpp lists them)",
                    folder, value);
      file.shader = *shader;
    }
    else if (key == "surface")
    {
      const std::optional<shared::Surface_Kind> kind = shared::try_parse_surface_kind(value);
      if (!kind)
        fatal_error("material '{}': material.txt says surface '{}', which names no Surface_Kind "
                    "(surface_kind.hpp lists them)",
                    folder, value);
      file.surface = *kind;
    }
    else
    {
      fatal_error("material '{}': material.txt key '{}' is not one material_file.hpp knows", folder, key);
    }
  }
  return file;
}

} // namespace assets
