#pragma once

#include "log.hpp"
#include <algorithm>
#include <charconv>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace cvar
{

// Forward declare
struct ICVar;

// The Registry Singleton
class CVarSystem
{
public:
  static CVarSystem &Get()
  {
    static CVarSystem instance;
    return instance;
  }

  void Register(const std::string &name, ICVar *cvar)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (registry_.find(name) != registry_.end())
    {
      log_error("CVar '{}' already registered", name);
      return;
    }
    registry_[name] = cvar;
  }

  ICVar *Find(const std::string &name)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registry_.find(name);
    if (it != registry_.end())
    {
      return it->second;
    }
    return nullptr;
  }

  // Helper to list all cvars (e.g. for a "list" command)
  void VisitAll(std::function<void(const std::string &, ICVar *)> visitor)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[name, cvar] : registry_)
    {
      visitor(name, cvar);
    }
  }

private:
  std::unordered_map<std::string, ICVar *> registry_;
  std::mutex mutex_;
};

namespace flags
{
enum : uint64_t
{
  None = 0,
  Admin = 1 << 0,
  Client = 1 << 1,
  Cheat = 1 << 2,
};
}

// Type-erased base interface for all CVars
struct ICVar
{
  ICVar(const std::string &name, const std::string &desc,
        uint64_t flags = flags::None)
      : name_(name), description_(desc), flags_(flags)
  {
    CVarSystem::Get().Register(name, this);
  }
  virtual ~ICVar() = default;

  virtual std::string GetString() const = 0;
  virtual void SetFromString(const std::string &val) = 0;

  const std::string &GetName() const { return name_; }
  const std::string &GetDescription() const { return description_; }
  uint64_t GetFlags() const { return flags_; }

protected:
  std::string name_;
  std::string description_;
  uint64_t flags_;
};

// Typed implementation
template <typename T> class CVar : public ICVar
{
public:
  using OnChangeCallback = std::function<void(const T &newValue)>;

  CVar(const std::string &name, T defaultValue, const std::string &desc = "",
       uint64_t flags = flags::None, OnChangeCallback cb = nullptr)
      : ICVar(name, desc, flags), value_(defaultValue), callback_(cb)
  {
  }

  // Direct access for C++ code (High performance)
  const T &Get() const
  {
    // We could make this atomic if we expect thread-safety issues on the value
    // itself, but for most game loops, reading a global int/float is fine or
    // handled via other sync. For std::string, we probably want a lock if
    // frequent writes happen from other threads.
    return value_;
  }

  void Set(const T &val)
  {
    value_ = val;
    if (callback_)
      callback_(value_);
  }

  // --- ICVar Implementation ---

  std::string GetString() const override
  {
    if constexpr (std::is_same_v<T, std::string>)
    {
      return value_;
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
      return value_ ? "1" : "0";
    }
    else
    {
      return std::to_string(value_);
    }
  }

  void SetFromString(const std::string &str) override
  {
    if constexpr (std::is_same_v<T, std::string>)
    {
      Set(str);
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
      // "true", "1", "yes" -> true
      std::string tmp = str;
      std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::tolower);
      bool val = (tmp == "1" || tmp == "true" || tmp == "yes" || tmp == "on");
      Set(val);
    }
    else
    {
      // int, float, double
      T val = T();
      std::stringstream ss(str);
      ss >> val;
      if (!ss.fail())
      {
        Set(val);
      }
      else
      {
        log_error("Unrecognized format for cvar '{}': {}", name_, str);
      }
    }
  }

  // helper for direct assignment
  CVar<T> &operator=(const T &val)
  {
    Set(val);
    return *this;
  }

  // helper for implicit conversion
  operator T() const { return Get(); }

private:
  T value_;
  OnChangeCallback callback_;
};

} // namespace cvar
