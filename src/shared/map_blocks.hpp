#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

// ============================================================================
// The .source file's block grammar, and nothing above it.
//
//   map_file  := block*
//   block     := keyword '{' member* '}'
//   member    := property | block
//   property  := string string                 -- key, then value
//   keyword   := 'entity' | 'cvars' | 'materials' | <a geometry kind>
//   string    := '"' char* '"'                 -- no escapes; may contain spaces
//
// Tokens are whitespace-separated, so '{' and '}' must stand alone. This reader
// doesn't know the keyword set: it reads EVERY block to its closing brace and
// hands them all up, and parse_map_from_string reports-and-skips the ones it
// doesn't recognize. That's what makes a map written by a newer build degrade to
// "missing some objects" on an older one instead of derailing the whole parse.
//
// A member is a property or a nested block, told apart by ONE token: a key is
// followed by its value, a nested keyword by '{'. A property value of "{" is
// written quoted and so cannot be mistaken for one.
//
// Nesting exists for `face` blocks under `brush` (see map_geometry.hpp). A brush
// with no face blocks loads exactly as it did before they existed, which is what
// makes the format change backward-compatible on read with no version number.
//
// This lives in its own translation unit because both halves of the file need
// it: map.cpp owns which keywords mean what, and map_geometry.cpp owns the
// inside of a geometry block including its children.
// ============================================================================

namespace shared
{

// A block on the way IN. Properties are a map, so one key appears at most once
// and the order in the file is not preserved.
struct map_block_t
{
  std::string keyword;
  // For 'entity' blocks this holds "classname" alongside the rest, exactly as
  // written; the entity path pulls it out itself.
  std::map<std::string, std::string> properties;
  std::vector<map_block_t> children;
};

// A block on the way OUT. Properties are an ordered vector, not a map, so a
// writer can emit them in declaration order and the file stays git-diffable.
struct map_block_out_t
{
  std::string keyword;
  std::vector<std::pair<std::string, std::string>> properties;
  std::vector<map_block_out_t> children;
};

std::vector<map_block_t> parse_map_content(const std::string &content);

std::string serialize_map_blocks(const std::vector<map_block_out_t> &blocks);

} // namespace shared
