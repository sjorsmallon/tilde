#include "trigger_action_registry.hpp"

#include <algorithm>

namespace server
{

Trigger_Action_Registry &Trigger_Action_Registry::get()
{
  static Trigger_Action_Registry instance;
  return instance;
}

void Trigger_Action_Registry::register_action(const std::string &name,
                                              trigger_action_fn fn)
{
  registry_[name] = fn;
}

trigger_action_fn
Trigger_Action_Registry::find_action(const std::string &name) const
{
  auto it = registry_.find(name);
  return (it == registry_.end()) ? nullptr : it->second;
}

std::vector<std::string> Trigger_Action_Registry::list_action_names() const
{
  std::vector<std::string> names;
  names.reserve(registry_.size());
  for (const auto &[name, fn] : registry_)
    names.push_back(name);
  std::sort(names.begin(), names.end());
  return names;
}

Trigger_Action_Registration::Trigger_Action_Registration(
    const std::string &name, trigger_action_fn fn)
{
  Trigger_Action_Registry::get().register_action(name, fn);
}

} // namespace server
