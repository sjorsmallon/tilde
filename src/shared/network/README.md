# Network Serialization System

> **⚠ The `SCHEMA_FIELD` / `Schema_Registry` half of this is being replaced.**
> Entity and component field declarations move to `entities.def` + the
> `def_gen` build-time generator; the flat `Field_Prop` walk described below
> is replaced by generated per-type serializers ("serializer v2"). The bitstream
> and delta-compression design survives unchanged — it is the encoding side of
> the detection/encoding seam. See `entity_def.md` at the repo root.

# DELTA COMPRESSION SIDENOTE
The baseline decides *which fields get written*, never *what value is written*. Every value on the wire is absolute. write_records walks networked_leaf_fields(type), compares each leaf against the baseline's copy, sets a mask bit where they differ, and then writes that field's current content in full. There's no subtraction anywhere.

Two things fall out of that, and both are load-bearing:

Errors can't accumulate. Since values are absolute, applying a packet to the correct baseline gives you the server's exact bytes, full stop. If deltas were arithmetic, position is quantized to 1/32 per axis by write_coord — that rounding error would compound every single tick and you'd drift away from the server forever with no mechanism to notice.









This directory contains a custom network serialization system inspired by Source 2's design. It focuses on bandwidth efficiency and ease of use for gameplay programmers.

## Core Concepts

The system is built on three main pillars: **NetworkTypes**, **Schema**, and **Delta Encoding**.

### 1. Network_Var & Schema
To avoid writing manual `serialize()` and `deserialize()` functions for every entity, we use a reflection-like system called a **Schema**.

- **Network_Var<T>**: A wrapper around standard types (e.g., `int32`, `float`) that marks them as "networked".
- **Schema**: A static description of a class that tells the system *where* in memory these variables are located.

#### Why Track Offsets?
The generic serializer (`Entity`) doesn't know about your specific `Player` or `Weapon` class. It only sees a block of raw memory. The Schema provides a map:

> "Field 'health' is an `int32` located at **offset 12** from the start of the object."

This allows the serializer to generic loop over fields:
```cpp
// Generic serialization logic
uint8* entityBase = (uint8*)this;
for (const auto& field : schema) {
    void* data = entityBase + field.offset; 
    // Write data...
}
```

### 2. The BitStream (Wire Format)
We do not send full objects every tick. Instead, we send a compact **Bit Stream** representing the inputs or state.

The stream is a sequence of bits that corresponds directly to the fields defined in the Schema.

### 3. Delta Encoding
To save massive amounts of bandwidth, we only send what has **changed**.

When serializing, the system compares the **Current State** against a **Baseline** (the 
last state the client acknowledged).

1. **Inherit from Entity**:
   ```cpp
   class My_Entity : public network::Entity { ... }
   ```

2. **Define Network Variables**:
   ```cpp
   Network_Var<int32> health;
   Network_Var<float> speed;
   ```

3. **Register the Schema**:
   Use the macros in your class definition and implementation.
   ```cpp
   // Header
   DECLARE_SCHEMA(My_Entity)

   // Source
   BEGIN_SCHEMA(My_Entity)
       DEFINE_FIELD(health, network::Field_Type::Int32)
       DEFINE_FIELD(speed, network::Field_Type::Float32)
   END_SCHEMA(My_Entity)
   ```

4. **Registration** happens automatically via static initializers — no manual
   `register_schema()` call needed.

### Nested / Composable Schemas

Any struct with `DECLARE_COMPONENT_SCHEMA` can be used as a field inside another
schema via `SCHEMA_FIELD`. The system detects nested schema types automatically
(via `has_schema_v<T>`) and handles serialization, deserialization, delta
compression, and editor widgets recursively — no special-casing required.

```cpp
// Component (components.hpp)
struct material_t {
    SCHEMA_FIELD_DEFAULT(vec3f, color, ..., (vec3f{1,1,1}));
    DECLARE_COMPONENT_SCHEMA(material_t)
};
SCHEMA_NAME_FOR_TYPE(material_t)

// Used inside another component or entity
struct render_component_t {
    SCHEMA_FIELD(material_t, material, ...);   // just works
    DECLARE_COMPONENT_SCHEMA(render_component_t)
};
```

To add a new component: declare fields with `SCHEMA_FIELD`, add
`DECLARE_COMPONENT_SCHEMA`, and `SCHEMA_NAME_FOR_TYPE` at namespace scope.
No `Schema_Type_Info` specialization or custom serialize code needed.

**Init order**: `REGISTER_SCHEMA_FIELD` calls `ensure_schema_registered<T>()`
for nested types, so the child schema is always registered before the parent
that uses it — regardless of static initializer ordering across translation units.


# The wire format and its confusion.

The thing I got confused about for the longest time is how to actually pack the entities
for the entity bundle. what I have settled on is the following:

1) write the entity ID / class
2) write the BITMASK of which fields have changed. this means we DO NOT have to encode the field id / type whatever _into_ the byte stream buffer. although I guess we cannot arbitrarily seek to whicever fields but I don't care about that now.
3) write the data using the new functions in entity_serialization. They do not specifically do delta compression but they do support varint / float compression.
floats are supported up to 5 digits (2^5 = 32, which is the multiplier you see everywhere in that code!) and floats are stored in an "has_value" (is not 0.0), "has integer value" (e.g. 1.23, not 0.23), "has float value" (e.g. the "12345" in "0.12345").
Calculation:
Value: 0.123456
Scaled: 0.123456 * 32 = 3.950592
Rounded: 4 (closest integer)
Reconstructed: 4 / 32 = 0.125

The integer part is way more clever than I initially understood it to be. apparently, a normal way to pack a varint is to write 1 bit for like "keep going", and then 4 subsequent bits for the byte value that follows. so you decompose the integer into constituent bytes and you just string them along.

