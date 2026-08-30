#include "map_blocks.hpp"

#include "log.hpp"

#include <sstream>

namespace shared
{

namespace
{

// Strip the surrounding quotes from a token, if it has them.
std::string unquote(std::string token)
{
  if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
    return token.substr(1, token.size() - 2);
  return token;
}

// Reads members until the closing brace. The opening brace has been consumed.
void parse_block_body(std::stringstream &stream, map_block_t &block)
{
  std::string token;
  while (stream >> token)
  {
    if (token == "}")
      return;

    const std::string key = unquote(token);

    std::string value;
    if (!(stream >> value))
    {
      log_error("map parse: key \"{}\" in block \"{}\" has no value", key, block.keyword);
      return;
    }

    if (value == "{")
    {
      map_block_t &child = block.children.emplace_back();
      child.keyword      = key;
      parse_block_body(stream, child);
      continue;
    }

    // A quoted value may contain spaces ("0 0 0"), so keep consuming tokens
    // until the closing quote.
    if (!value.empty() && value.front() == '"')
    {
      while (value.back() != '"' && !stream.eof())
      {
        std::string part;
        stream >> part;
        value += " " + part;
      }
      value = unquote(value);
    }

    block.properties[key] = value;
  }

  log_error("map parse: block \"{}\" ends without a closing '}}'", block.keyword);
}

void write_block(std::stringstream &stream, const map_block_out_t &block, int depth)
{
  const std::string indent(depth * 2, ' ');
  stream << indent << block.keyword << "\n" << indent << "{\n";
  for (const auto &[key, value] : block.properties)
    stream << indent << "  \"" << key << "\" \"" << value << "\"\n";
  for (const map_block_out_t &child : block.children)
    write_block(stream, child, depth + 1);
  stream << indent << "}\n";
}

} // namespace

std::vector<map_block_t> parse_map_content(const std::string &content)
{
  std::vector<map_block_t> blocks;
  std::stringstream stream(content);
  std::string token;

  while (stream >> token)
  {
    map_block_t block;
    block.keyword = token;

    std::string brace;
    if (!(stream >> brace) || brace != "{")
    {
      log_error("map parse: block \"{}\" is not followed by '{{' (got \"{}\") — stopping",
                block.keyword, brace);
      break;
    }

    parse_block_body(stream, block);
    blocks.push_back(std::move(block));
  }

  return blocks;
}

std::string serialize_map_blocks(const std::vector<map_block_out_t> &blocks)
{
  std::stringstream stream;
  for (const map_block_out_t &block : blocks)
    write_block(stream, block, 0);
  return stream.str();
}

} // namespace shared
