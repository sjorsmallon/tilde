// The .source block grammar (shared/map/map_blocks.hpp): a string comes back exactly as it was written,
// whatever is in it, and a value that used to derail the reader no longer swallows the blocks after it.

#include "../shared/map/map_blocks.hpp"

#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool condition, const char* what)
{
  if (!condition)
  {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

int main()
{
  const std::vector<std::string> values = {
      "",
      "plain",
      "0 0 0",
      "                 \\",
      "   leading spaces",
      "trailing spaces   ",
      "two  spaces   inside",
      " ",
      "\"",
      "say \"hello\"",
      "back\\slash",
      "ends with a backslash\\",
      "\\\"",
      "{",
      "}",
      "line one\nline two\r\n\ttabbed",
      std::string("a zero\0byte", 11),
  };

  shared::map_block_out_t first;
  first.keyword = "entity";
  for (size_t index = 0; index < values.size(); ++index)
    first.properties.emplace_back("row_" + std::to_string(index), values[index]);
  first.properties.emplace_back("a key with \"quotes\"", "kept");

  shared::map_block_out_t child;
  child.keyword = "override";
  child.properties.emplace_back("speed", "500");
  first.children.push_back(child);

  shared::map_block_out_t second;
  second.keyword = "entity";
  second.properties.emplace_back("classname", "the block after the awkward one");

  const std::string text = shared::serialize_map_blocks({first, second});
  const std::vector<shared::map_block_t> blocks = shared::parse_map_content(text);

  check(blocks.size() == 2, "both blocks come back: an awkward string swallows nothing after it");
  if (blocks.size() == 2)
  {
    bool every_value_returns = true;
    for (size_t index = 0; index < values.size(); ++index)
    {
      const auto found = blocks[0].properties.find("row_" + std::to_string(index));
      const bool same  = found != blocks[0].properties.end() && found->second == values[index];
      if (!same)
        std::printf("  row_%zu did not come back as written\n", index);
      every_value_returns = every_value_returns && same;
    }
    check(every_value_returns, "every string comes back exactly as it was written");
    check(blocks[0].properties.size() == values.size() + 1, "no property is invented or lost");
    check(blocks[0].properties.count("a key with \"quotes\"") == 1, "a key is escaped as a value is");
    check(blocks[0].children.size() == 1 && blocks[0].children[0].keyword == "override" &&
              blocks[0].children[0].properties.count("speed") == 1,
          "a nested block is still a nested block");
    check(blocks[1].properties.count("classname") == 1, "the block after keeps its properties");
  }

  {
    const std::vector<shared::map_block_t> spaced =
        shared::parse_map_content("entity\n{\n\n   \"origin\"     \"1 2 3\"\n\n}\n\n\nentity\n{\n}\n");
    check(spaced.size() == 2 && spaced[0].properties.count("origin") == 1 &&
              spaced[0].properties.at("origin") == "1 2 3",
          "whitespace between tokens means nothing");
  }

  {
    const std::vector<shared::map_block_t> open =
        shared::parse_map_content("entity\n{\n  \"name\" \"never closed\n}\nentity\n{\n}\n");
    check(open.size() == 1 && open[0].properties.empty(), "a string with no closing quote ends the read");
  }

  if (failures == 0)
    std::printf("map_blocks_test: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
