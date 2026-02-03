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
  size_t offset;
  size_t size;
  Field_Type type;
};

bool parse_string_to_field(const std::string &value, Field_Type type,
                           void *out_ptr);

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
  props.push_back({#MemberName, offsetof(ThisClass, MemberName),               \
                   sizeof(ThisClass::MemberName), TypeEnum});

#define END_SCHEMA(ClassName)                                                  \
  network::Schema_Registry::get().register_class(#ClassName, props);           \
  }                                                                            \
  const network::Class_Schema *ClassName::get_schema() const                   \
  {                                                                            \
    return network::Schema_Registry::get().get_schema(#ClassName);             \
  }

} // namespace network
