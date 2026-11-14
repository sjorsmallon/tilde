#include "map_blocks.hpp"

#include "log.hpp"

#include <cctype>
#include <optional>
#include <sstream>

namespace shared
{

namespace
{

struct token_t
{
  std::string text;
  bool        quoted = false;
};

struct reader_t
{
  const std::string &content;
  size_t             position = 0;
};

bool is_brace(const token_t &token, char brace)
{
  return !token.quoted && token.text.size() == 1 && token.text[0] == brace;
}

// The inside of a string, the opening quote already consumed. Every character is kept as written, spaces included.
std::optional<token_t> try_read_quoted_token(reader_t &reader)
{
  token_t token;
  token.quoted = true;

  while (reader.position < reader.content.size())
  {
    const char character = reader.content[reader.position++];
    if (character == '"')
      return token;
    if (character != '\\')
    {
      token.text += character;
      continue;
    }

    if (reader.position >= reader.content.size())
      break;
    const char escaped = reader.content[reader.position++];
    switch (escaped)
    {
    case '"':  token.text += '"';  break;
    case '\\': token.text += '\\'; break;
    case 'n':  token.text += '\n'; break;
    case 'r':  token.text += '\r'; break;
    case 't':  token.text += '\t'; break;
    case '0':  token.text += '\0'; break;
    default:
      log_error("map parse: unknown escape '\\{}' in a string, kept as written", escaped);
      token.text += '\\';
      token.text += escaped;
      break;
    }
  }

  log_error("map parse: a string starting \"{}\" has no closing quote", token.text.substr(0, 40));
  return std::nullopt;
}

std::optional<token_t> try_read_token(reader_t &reader)
{
  while (reader.position < reader.content.size() &&
         std::isspace((unsigned char)reader.content[reader.position]))
    ++reader.position;
  if (reader.position >= reader.content.size())
    return std::nullopt;

  if (reader.content[reader.position] == '"')
  {
    ++reader.position;
    return try_read_quoted_token(reader);
  }

  token_t token;
  while (reader.position < reader.content.size() &&
         !std::isspace((unsigned char)reader.content[reader.position]))
    token.text += reader.content[reader.position++];
  return token;
}

// Reads members until the closing brace. The opening brace has been consumed.
void parse_block_body(reader_t &reader, map_block_t &block)
{
  while (std::optional<token_t> key = try_read_token(reader))
  {
    if (is_brace(*key, '}'))
      return;

    const std::optional<token_t> value = try_read_token(reader);
    if (!value)
    {
      log_error("map parse: key \"{}\" in block \"{}\" has no value", key->text, block.keyword);
      return;
    }

    if (is_brace(*value, '{'))
    {
      map_block_t &child = block.children.emplace_back();
      child.keyword      = key->text;
      parse_block_body(reader, child);
      continue;
    }

    block.properties[key->text] = value->text;
  }

  log_error("map parse: block \"{}\" ends without a closing '}}'", block.keyword);
}

std::string escape_string(const std::string &text)
{
  std::string escaped;
  escaped.reserve(text.size());
  for (const char character : text)
  {
    switch (character)
    {
    case '"':  escaped += "\\\""; break;
    case '\\': escaped += "\\\\"; break;
    case '\n': escaped += "\\n";  break;
    case '\r': escaped += "\\r";  break;
    case '\t': escaped += "\\t";  break;
    case '\0': escaped += "\\0";  break;
    default:   escaped += character; break;
    }
  }
  return escaped;
}

void write_block(std::stringstream &stream, const map_block_out_t &block, int depth)
{
  const std::string indent(depth * 2, ' ');
  stream << indent << block.keyword << "\n" << indent << "{\n";
  for (const auto &[key, value] : block.properties)
    stream << indent << "  \"" << escape_string(key) << "\" \"" << escape_string(value) << "\"\n";
  for (const map_block_out_t &child : block.children)
    write_block(stream, child, depth + 1);
  stream << indent << "}\n";
}

} // namespace

std::vector<map_block_t> parse_map_content(const std::string &content)
{
  std::vector<map_block_t> blocks;
  reader_t                 reader{content};

  while (std::optional<token_t> keyword = try_read_token(reader))
  {
    map_block_t block;
    block.keyword = keyword->text;

    const std::optional<token_t> brace = try_read_token(reader);
    if (!brace || !is_brace(*brace, '{'))
    {
      log_error("map parse: block \"{}\" is not followed by '{{' (got \"{}\") — stopping",
                block.keyword, brace ? brace->text : std::string{});
      break;
    }

    parse_block_body(reader, block);
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
