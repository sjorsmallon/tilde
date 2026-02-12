#pragma once

#include "network_types.hpp"
#include <cstddef>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace network
{

// --- Schema Flags ---

enum Schema_Flags : uint32_t
{
  None = 0,
  Networked = 1 << 0, // Field is synchronized over the network
  Editable = 1 << 1,  // Field is editable in the editor
  Saveable = 1 << 2,  // Field is saved to disk
};

constexpr Schema_Flags operator|(Schema_Flags a, Schema_Flags b)
{
  return static_cast<Schema_Flags>(static_cast<uint32_t>(a) |
                                   static_cast<uint32_t>(b));
}

constexpr bool has_flag(Schema_Flags flags, Schema_Flags flag)
{
  return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

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
  PascalString,
  RenderComponent,
};

struct Field_Prop
{
  std::string name;
  uint32_t index; // Unique index for this class
  size_t offset;
  size_t size;
  Field_Type type;
  Schema_Flags flags;
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
          std::cerr << "Error: Field size mismatch applying diff for field "
                    << field.name << "\n";
        }
        break;
      }
    }
  }
}

// --- Type to Field_Type mapping ---
// Note: int32/float32/vec3f are typedefs so no need for separate
// int/float/linalg::vec3

template <typename T> struct Schema_Type_Info;

template <> struct Schema_Type_Info<int32>
{
  static constexpr Field_Type type = Field_Type::Int32;
};

template <> struct Schema_Type_Info<float32>
{
  static constexpr Field_Type type = Field_Type::Float32;
};

template <> struct Schema_Type_Info<bool>
{
  static constexpr Field_Type type = Field_Type::Bool;
};

template <> struct Schema_Type_Info<vec3f>
{
  static constexpr Field_Type type = Field_Type::Vec3f;
};

template <> struct Schema_Type_Info<pascal_string>
{
  static constexpr Field_Type type = Field_Type::PascalString;
};

template <> struct Schema_Type_Info<render_component_t>
{
  static constexpr Field_Type type = Field_Type::RenderComponent;
};

// --- Field metadata for compile-time storage ---

struct Field_Meta
{
  const char *name;
  size_t size;
  Field_Type type;
  Schema_Flags flags;
};

// --- Macros for Schema declaration ---

// Use in class header to declare schema functions
#define DECLARE_SCHEMA(ClassName)                                              \
public:                                                                        \
  static void register_schema();                                               \
  virtual const ::network::Class_Schema *get_schema() const;

// Declares a field AND adds it to the schema.
// Usage: SCHEMA_FIELD(vec3f, position, Schema_Flags::Networked |
// Schema_Flags::Editable); This macro expands to:
//   1. The field declaration itself
//   2. A static field metadata entry (without offset, computed at registration)
#define SCHEMA_FIELD(Type, Name, Flags)                                        \
  Type Name{};                                                                 \
  static constexpr ::network::Field_Meta _schema_meta_##Name = {               \
      #Name, sizeof(Type), ::network::Schema_Type_Info<Type>::type, Flags};

#define SCHEMA_FIELD_DEFAULT(Type, Name, Flags, Default)                        \
  Type Name = Default;                                                         \
  static constexpr ::network::Field_Meta _schema_meta_##Name = {               \
      #Name, sizeof(Type), ::network::Schema_Type_Info<Type>::type, Flags};

// Begin schema registration in .cpp file
#define BEGIN_SCHEMA(ClassName)                                                \
  void ClassName::register_schema()                                            \
  {                                                                            \
    using ThisClass = ClassName;                                               \
    std::vector<network::Field_Prop> props;

// Register a field that was declared with SCHEMA_FIELD
// Uses the stored metadata and computes offset
#define REGISTER_FIELD(MemberName)                                             \
  static_assert(std::is_trivially_copyable_v<decltype(ThisClass::MemberName)>, \
                "Field " #MemberName                                           \
                " must be trivially copyable for Undo/Redo/Networking");       \
  props.push_back({ThisClass::_schema_meta_##MemberName.name,                  \
                   (uint32_t)props.size(), offsetof(ThisClass, MemberName),    \
                   ThisClass::_schema_meta_##MemberName.size,                  \
                   ThisClass::_schema_meta_##MemberName.type,                  \
                   ThisClass::_schema_meta_##MemberName.flags});

// End schema registration
#define END_SCHEMA(ClassName)                                                  \
  network::Schema_Registry::get().register_class(#ClassName, props);           \
  }                                                                            \
  const network::Class_Schema *ClassName::get_schema() const                   \
  {                                                                            \
    return network::Schema_Registry::get().get_schema(#ClassName);             \
  }                                                                            \
  namespace                                                                    \
  {                                                                            \
  struct ClassName##_Schema_Init                                               \
  {                                                                            \
    ClassName##_Schema_Init() { ClassName::register_schema(); }                \
  };                                                                           \
  static ClassName##_Schema_Init g_##ClassName##_schema_init;                  \
  }

// --- New inheritance-aware schema macros ---

// Helper to get parent schema fields (empty for base classes)
#define _SCHEMA_INHERIT_FIELDS_1(ClassName)
#define _SCHEMA_INHERIT_FIELDS_2(ClassName, ParentClass)                       \
  {                                                                            \
    const auto *parent_schema =                                                \
        network::Schema_Registry::get().get_schema(#ParentClass);              \
    if (parent_schema)                                                         \
    {                                                                          \
      for (const auto &pf : parent_schema->fields)                             \
      {                                                                        \
        props.push_back({pf.name, (uint32_t)props.size(), pf.offset, pf.size,  \
                         pf.type, pf.flags});                                  \
      }                                                                        \
    }                                                                          \
  }

// Macro argument counting
#define _SCHEMA_NARG(...) _SCHEMA_NARG_(__VA_ARGS__, _SCHEMA_RSEQ_N())
#define _SCHEMA_NARG_(...) _SCHEMA_ARG_N(__VA_ARGS__)
#define _SCHEMA_ARG_N(_1, _2, N, ...) N
#define _SCHEMA_RSEQ_N() 2, 1, 0

// Concatenation helpers
#define _SCHEMA_CAT(a, b) _SCHEMA_CAT_(a, b)
#define _SCHEMA_CAT_(a, b) a##b

// Dispatch to the correct inherit fields macro
#define _SCHEMA_INHERIT_FIELDS(...)                                            \
  _SCHEMA_CAT(_SCHEMA_INHERIT_FIELDS_, _SCHEMA_NARG(__VA_ARGS__))(__VA_ARGS__)

// Main macro: DEFINE_SCHEMA_CLASS(ClassName) or DEFINE_SCHEMA_CLASS(ClassName,
// ParentClass) Usage in .cpp:
//   DEFINE_SCHEMA_CLASS(Player_Entity, Entity)
//   {
//       BEGIN_SCHEMA_FIELDS()
//           REGISTER_SCHEMA_FIELD(health);
//       END_SCHEMA_FIELDS()
//   }
#define DEFINE_SCHEMA_CLASS(ClassName, ...)                                    \
  const network::Class_Schema *ClassName::get_schema() const                   \
  {                                                                            \
    return network::Schema_Registry::get().get_schema(#ClassName);             \
  }                                                                            \
  namespace                                                                    \
  {                                                                            \
  struct ClassName##_Schema_Init                                               \
  {                                                                            \
    ClassName##_Schema_Init() { ClassName::register_schema(); }                \
  };                                                                           \
  static ClassName##_Schema_Init g_##ClassName##_schema_init;                  \
  }                                                                            \
  void ClassName::register_schema()                                            \
  {                                                                            \
    using ThisClass = ClassName;                                               \
    static constexpr const char *_schema_class_name = #ClassName;              \
    (void)sizeof(ThisClass);                                                   \
    std::vector<network::Field_Prop> props;                                    \
    _SCHEMA_INHERIT_FIELDS(ClassName __VA_OPT__(, ) __VA_ARGS__)

#define BEGIN_SCHEMA_FIELDS()

#define REGISTER_SCHEMA_FIELD(MemberName)                                      \
  static_assert(std::is_trivially_copyable_v<decltype(ThisClass::MemberName)>, \
                "Field " #MemberName                                           \
                " must be trivially copyable for Undo/Redo/Networking");       \
  props.push_back({ThisClass::_schema_meta_##MemberName.name,                  \
                   (uint32_t)props.size(), offsetof(ThisClass, MemberName),    \
                   ThisClass::_schema_meta_##MemberName.size,                  \
                   ThisClass::_schema_meta_##MemberName.type,                  \
                   ThisClass::_schema_meta_##MemberName.flags});

#define END_SCHEMA_FIELDS()                                                    \
  network::Schema_Registry::get().register_class(_schema_class_name, props);   \
  }

} // namespace network
