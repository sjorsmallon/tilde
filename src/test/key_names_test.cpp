// Every key the input layer knows can be bound and written back: key_name and
// try_key_from_name round-trip, names are unique, and a typo is refused.

#include "client/key_names.hpp"

#include <cstdio>
#include <set>
#include <string>

using client::input::key_t;

namespace
{

int failure_count = 0;

void check(bool condition, const std::string& what)
{
  if (condition)
    return;
  std::printf("  FAILED: %s\n", what.c_str());
  ++failure_count;
}

} // namespace

int main()
{
  std::printf("=== key_names_test ===\n");

  std::set<std::string_view> seen;
  for (uint32_t index = 1; index < (uint32_t)key_t::Count; ++index)
  {
    const key_t            key  = (key_t)index;
    const std::string_view name = client::input::key_name(key);
    const std::string      label(name);

    check(!name.empty(), "key " + std::to_string(index) + " has a name");
    check(seen.insert(name).second, "'" + label + "' names one key");

    const std::optional<key_t> parsed = client::input::try_key_from_name(name);
    check(parsed && *parsed == key, "'" + label + "' parses back to its key");
  }

  check(client::input::try_key_from_name("F3") == key_t::F3, "names are case-insensitive");
  check(client::input::try_key_from_name("R") == key_t::R, "a capital letter binds its key");
  check(!client::input::try_key_from_name("unknown"), "Unknown is not bindable");
  check(!client::input::try_key_from_name("f13"), "a key that does not exist is refused");
  check(!client::input::try_key_from_name(""), "an empty name is refused");

  if (failure_count != 0)
  {
    std::printf("\nkey_names_test FAILED with %d failure(s)\n", failure_count);
    return 1;
  }
  std::printf("key_names_test: all checks passed\n");
  return 0;
}
