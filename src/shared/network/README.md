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

When serializing, the system compares the **Current State** against a **Baseline** (the last state the client acknowledged).

#### detailed Wire Format Example
Imagine an Entity with two fields: `Health` (int) and `Ammo` (int).

**Scenario 1: Full Update (New Entity)**
The client knows nothing. Everything is considered "changed".
- **Stream**: `[1][100] [1][30]`
    - `1` (Bit): Field 1 (Health) Changed? **YES**.
    - `100` (Bytes): The value of Health.
    - `1` (Bit): Field 2 (Ammo) Changed? **YES**.
    - `30` (Bytes): The value of Ammo.

**Scenario 2: Delta Update (Health Changed)**
The player took damage (100 -> 90). Ammo is unchanged.
- **Stream**: `[1][90] [0]`
    - `1` (Bit): Field 1 (Health) Changed? **YES**.
    - `90` (Bytes): The new value.
    - `0` (Bit): Field 2 (Ammo) Changed? **NO**.
    - *(Data for Ammo is skipped entirely)*.

### How to Use

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
