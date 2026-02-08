#pragma once

#include "network_types.hpp"
#include <cstddef>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace network
{

// --- Schema Definition ---

struct Field_Update
{
  uint16_t field_id;         // Index from your schema
  std::vector<uint8_t> data; // The raw bytes of the new value
};

enum class Field_Type
{
  Int32,
  Float32,
  Bool,
  Vec3f,
  // Add more as needed
};

struct Field_Prop
{
  std::string name;
  uint32_t index; // New: Unique index for this class
  size_t offset;
  size_t size;
  Field_Type type;
};

bool parse_string_to_field(const std::string &value, Field_Type type,
                           void *out_ptr);

bool serialize_field_to_string(const void *in_ptr, Field_Type type,
                               std::string &out_value);

struct Class_Schema
{
  std::string class_name;
  std::vector<Field_Prop> fields;
};

class Schema_Registry
{
public:
  static Schema_Registry &get()
  {
    static Schema_Registry instance;
    return instance;
  }

  void register_class(const std::string &name,
                      const std::vector<Field_Prop> &fields)
  {
    schemas[name] = {name, fields};
    // In a real system, you might build a flat lookup table here
  }

  const Class_Schema *get_schema(const std::string &name)
  {
    if (schemas.find(name) != schemas.end())
    {
      return &schemas[name];
    }
    return nullptr;
  }

private:
  std::unordered_map<std::string, Class_Schema> schemas;
};

// Generates a list of updates to transform 'baseline' into 'current'
// based on the provided schema.
inline std::vector<Field_Update> diff(const void *baseline, const void *current,
                                      const Class_Schema *schema)
{
  std::vector<Field_Update> updates;
  const uint8_t *base_ptr = static_cast<const uint8_t *>(baseline);
  const uint8_t *curr_ptr = static_cast<const uint8_t *>(current);

  for (const auto &field : schema->fields)
  {
    if (std::memcmp(base_ptr + field.offset, curr_ptr + field.offset,
                    field.size) != 0)
    {
      Field_Update update;
      update.field_id = (uint16_t)field.index;
      update.data.resize(field.size);
      std::memcpy(update.data.data(), curr_ptr + field.offset, field.size);
      updates.push_back(std::move(update));
    }
  }
  return updates;
}

struct field_change_t
{
  uint16_t id;
  std::vector<uint8_t> old_val;
  std::vector<uint8_t> new_val;
};

// Generates a list of reversible changes to transform 'baseline' into 'current'
// capturing both old and new values.
inline std::vector<field_change_t> diff_reversible(const void *baseline,
                                                   const void *current,
                                                   const Class_Schema *schema)
{
  std::vector<field_change_t> changes;
  const uint8_t *base_ptr = static_cast<const uint8_t *>(baseline);
  const uint8_t *curr_ptr = static_cast<const uint8_t *>(current);

  for (const auto &field : schema->fields)
  {
    if (std::memcmp(base_ptr + field.offset, curr_ptr + field.offset,
                    field.size) != 0)
    {
      field_change_t change;
      change.id = (uint16_t)field.index;

      change.old_val.resize(field.size);
      std::memcpy(change.old_val.data(), base_ptr + field.offset, field.size);

      change.new_val.resize(field.size);
      std::memcpy(change.new_val.data(), curr_ptr + field.offset, field.size);

      changes.push_back(std::move(change));
    }
  }
  return changes;
}

// Applies a list of updates to 'target' based on the provided schema.
inline void apply_diff(void *target, const std::vector<Field_Update> &updates,
                       const Class_Schema *schema)
{
  uint8_t *target_ptr = static_cast<uint8_t *>(target);
  for (const auto &update : updates)
  {
    // Find field by index (O(N) but N is small, usually < 20)
    // Could optimize schema to have index -> field lookup if needed
    for (const auto &field : schema->fields)
    {
      if (field.index == update.field_id)
      {
        if (update.data.size() == field.size)
        {
          std::memcpy(target_ptr + field.offset, update.data.data(),
                      field.size);
        }
        else
        {
          // Size mismatch error?
          std::cerr << "Error: Field size mismatch applying diff for field "
                    << field.name << "\n";
        }
        break;
      }
    }
  }
}

// --- Network Variable Wrapper ---

template <typename T> struct Network_Var
{
  T value;

  Network_Var() : value{} {}
  Network_Var(T v) : value{v} {}

  Network_Var &operator=(const T &v)
  {
    value = v;
    return *this;
  }

  operator T() const { return value; }
  T *operator&() { return &value; }
  const T *operator&() const { return &value; }
};

// --- Macros for Schema declaration ---

#define DECLARE_SCHEMA(ClassName)                                              \
public:                                                                        \
  static void register_schema();                                               \
  virtual const network::Class_Schema *get_schema() const;

#define BEGIN_SCHEMA(ClassName)                                                \
  void ClassName::register_schema()                                            \
  {                                                                            \
    using ThisClass = ClassName;                                               \
    std::vector<network::Field_Prop> props;

#define DEFINE_FIELD(MemberName, TypeEnum)                                     \
  static_assert(std::is_trivially_copyable_v<decltype(ThisClass::MemberName)>, \
                "Field " #MemberName                                           \
                " must be trivially copyable for Undo/Redo/Networking");       \
  props.push_back({#MemberName, (uint32_t)props.size(),                        \
                   offsetof(ThisClass, MemberName),                            \
                   sizeof(ThisClass::MemberName), TypeEnum});

#define END_SCHEMA(ClassName)                                                  \
  network::Schema_Registry::get().register_class(#ClassName, props);           \
  }                                                                            \
  const network::Class_Schema *ClassName::get_schema() const                   \
  {                                                                            \
    return network::Schema_Registry::get().get_schema(#ClassName);             \
  }                                                                            \
  namespace                                                                    \
  {                                                                            \
  /* Register schema on startup. */                                            \
  /* Unlike Entities which use X-Macros for auto-registration, these */        \
  /* structs (aabb_t, etc.) need explicit registration at runtime. */          \
  struct ClassName##_Schema_Init                                               \
  {                                                                            \
    ClassName##_Schema_Init() { ClassName::register_schema(); }                \
  };                                                                           \
  static ClassName##_Schema_Init g_##ClassName##_schema_init;                  \
  }

} // namespace network
