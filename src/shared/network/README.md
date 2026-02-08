# Network Serialization System

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

4. **Register at Startup**:
   Call `My_Entity::register_schema()` once at the start of your game.


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

