#pragma once

// reflection -- the record types every .def family's generated tables are made
// of. Global scope, beside the other house types (Span, Array, enum_traits) and
// for the same reason: they belong to no one family.
//
// A generated table is pure data -- names, offsets, sizes, flags. What walks it
// (the map file's text conversion, the wire codec, the editor inspector) does
// not care WHICH family the row came from, so the row type cannot live inside
// one. entities_generated.hpp and the event families' headers all emit rows of
// the same field_info_t, and network/field_codec.hpp encodes any of them.
//
// What is NOT here: the per-family tag enums (entity_type, enum_type, the
// channel enums) and the tables themselves. Those are generated, one set per
// family, and they are what a field_info_t points into.

#include "span.hpp"

#include <cstdint>
#include <string>

enum field_type_t : uint8_t
{
  FIELD_TYPE_INVALID = 0,
  FIELD_TYPE_F32, FIELD_TYPE_F64,
  FIELD_TYPE_U8, FIELD_TYPE_U16, FIELD_TYPE_U32, FIELD_TYPE_U64,
  FIELD_TYPE_I8, FIELD_TYPE_I16, FIELD_TYPE_I32, FIELD_TYPE_I64,
  FIELD_TYPE_BOOL,
  FIELD_TYPE_V3, FIELD_TYPE_V4, FIELD_TYPE_V4I,
  FIELD_TYPE_STRING, FIELD_TYPE_ASSET, FIELD_TYPE_ENUM, FIELD_TYPE_COMPONENT,
};

// One declared enum, as a walker sees it: no type, just the names. Value N is
// value_names[N], so both directions are an index and a compare.
struct enum_type_info_t
{
  const char*             name;
  // Indexed by the enum's own numeric value; the values are dense and
  // start at 0, so `size()` is also the count of valid values.
  Span<const char* const> value_names;
};

// Four of field_info_t's columns are meaningful only for their own FIELD_TYPE.
// These name what "not that type" looks like, so a reader never has to remember
// whether absent is -1, 0 or null -- and so a check says what it means rather
// than testing a magic number.
constexpr int32_t                 NOT_A_COMPONENT    = -1;
constexpr uint32_t                NOT_A_STRING       = 0;
constexpr int32_t                 NOT_AN_ASSET_CLASS = -1;
constexpr const enum_type_info_t* NOT_AN_ENUM        = nullptr;

// A field of one struct. Offsets are relative to THAT struct, so walking into a
// component composes them by addition; the flat tables (the event families')
// never nest and their offsets are final.
struct field_info_t
{
  const char*  name;
  field_type_t type;
  uint32_t     offset;
  uint32_t     size_in_bytes;
  uint32_t     flags;
  int32_t      component_id;    // FIELD_TYPE_COMPONENT only, else NOT_A_COMPONENT
  uint32_t     string_capacity; // FIELD_TYPE_STRING only, else NOT_A_STRING
  int32_t      asset_class_id;  // FIELD_TYPE_ASSET only, else NOT_AN_ASSET_CLASS

  // FIELD_TYPE_ENUM only, else NOT_AN_ENUM. A pointer rather than a per-family
  // enum id: that is what lets one codec serve every family, since there is no
  // id space left to resolve against.
  const enum_type_info_t* enum_info;
};

// --- Text -------------------------------------------------------------------
//
// The ONLY place field bytes become characters. Map save and map load are the
// two callers today, plus the event debug formatter; anything that later needs
// a name-keyed textual form of a field (an undo log written to disk, a
// diagnostic dump) uses the same pair rather than growing a second encoding.
//
// Floats use the shortest representation that round-trips, so a save/load cycle
// is exact and a map file diff shows only what actually changed.

// Writes the field at `field_bytes` into `out_text`. False (out_text untouched)
// only for FIELD_TYPE_COMPONENT, which is not a leaf -- flatten first.
bool field_to_text(const void* field_bytes, const field_info_t& field, std::string& out_text);

// Parses `text` into the field at `field_bytes`. False, and the field left
// alone, when the text does not parse as this field's type or names an
// enum/asset value that does not exist. The caller reports it -- this returns
// the failure rather than logging, because only the caller knows which entity
// and which file it came from.
bool field_from_text(const std::string& text, const field_info_t& field, void* field_bytes);

// "{ name=value, name=value }" over a FLAT table -- the event families', whose
// offsets are final. An entity's table nests, so its caller flattens first and
// formats the leaves.
//
// This is the debug view (sv_event_debug / cl_event_debug), so a field that
// field_to_text refuses prints as "<unprintable>" rather than aborting the dump.
std::string fields_to_text(Span<const field_info_t> fields, const void* base);
