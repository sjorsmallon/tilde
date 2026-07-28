#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>
#include <utility>
#include <new>
#include <type_traits>

static constexpr size_t PAGE_SIZE = 4096;

template <typename Type, size_t Capacity>
struct Array
{
    static constexpr size_t capacity = Capacity;
    Type* data = new Type[Capacity];

    Array() = default;
    ~Array() { delete[] data; }
    // owns a raw allocation, so the implicit copy would double free.
    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;
    Array(Array&&) = delete;
    Array& operator=(Array&&) = delete;

    Type& operator[](size_t index) { return data[index]; }

    auto begin() -> Type* { return data; }
    auto end() -> Type* { return data + capacity; }
};

template <typename Type>
struct Vector
{
    Type* data = nullptr;
    size_t capacity = 0;
    size_t occupied = 0;
    Vector() = default;
    ~Vector() { delete[] data; }
    // explicitly delete other operators. add them when we need them.
    Vector(const Vector&) = delete;
    Vector& operator=(const Vector&) = delete;
    Vector(Vector&& other) = delete;
    Vector& operator=(Vector&& other) = delete;

    Type& operator[](size_t index) { return data[index]; }
    const Type& operator[](size_t index) const { return data[index]; }
    auto begin() -> Type* { return data; }
    auto end() -> Type* { return data + occupied; }

    void reserve(size_t new_capacity)
    {
        if (new_capacity <= capacity)
            return;

        Type* new_data = new Type[new_capacity];
        for (size_t index = 0; index < occupied; ++index)
            new_data[index] = std::move(data[index]);
        delete[] data;
        data = new_data;
        capacity = new_capacity;
    }

    void push_back(const Type& value)
    {
        if (occupied == capacity)
            reserve(capacity > 0 ? capacity * 2 : 8);
        data[occupied++] = value;
    }

    // keeps the allocation around for refilling.
    void clear() { occupied = 0; }

    // hands the allocation back.
    void reset()
    {
        delete[] data;
        data = nullptr;
        capacity = 0;
        occupied = 0;
    }
};

// A singly linked list of fixed-size buckets with per-bucket free slot tracking.
//
// Buckets are allocated one at a time and never move or reallocate, so an element's
// address and its index both stay valid until that element is freed. Growth is
// unbounded: allocate() appends a bucket when every existing one is full.
//
// Bucket_Bytes is the size of a whole bucket, header included, so a default bucket
// occupies exactly one page rather than a page plus a spilled header. Type must be
// default constructible and copy assignable.
//
// Bucket_Bytes is raised (never lowered) until a bucket holds at least
// minimum_slots_per_bucket elements, so a large Type gets a larger bucket instead of a
// degenerate three-slot one, and then rounded up to a power of two.
//
// free(Type*) returns an element without knowing its index. How it finds the owning
// bucket depends on how big a bucket ended up:
//   - up to maximum_bucket_alignment, the bucket is aligned to its own size, so
//     masking the element address lands on the bucket. No per-element cost.
//   - past that, alignment is not available (compilers cap alignas), so each slot
//     carries a back pointer to its bucket instead. That costs a pointer per slot,
//     which only happens for elements already large enough to make it noise.
// bucket_is_maskable says which regime an instantiation is in.
//
// Index layout is flat: index == bucket_ordinal * slots_per_bucket + slot_index.
// The list is the traversal order; a side table of bucket pointers makes resolving
// an index O(1). The table only ever holds pointers to buckets that do not move, so
// growing it costs a pointer copy and invalidates nothing.
constexpr size_t round_up_to_power_of_two(size_t value)
{
    size_t result = 1;
    while (result < value)
        result *= 2;
    return result;
}

template <typename Type, size_t Bucket_Bytes = PAGE_SIZE>
struct Stable_Array
{
    static constexpr uint32_t invalid_slot = UINT32_MAX;
    // enough that the header and the cost of an allocation stay noise. Buckets end up
    // well above this once the size is rounded up; it only rules out the degenerate case.
    static constexpr size_t minimum_slots_per_bucket = 16;

    // both clang and msvc reject alignas past this, which is what bounds masking.
    static constexpr size_t maximum_bucket_alignment = 8192;

    struct Bucket;

    struct Slot_Plain
    {
        Type value{}; // must stay first: free(Type*) casts an element back to its slot
        uint32_t next_free = invalid_slot; // only meaningful while !valid
        bool valid = false;
    };

    struct Slot_With_Owner : Slot_Plain
    {
        Bucket* owner = nullptr; // only for buckets too large to align and mask
    };

    // split out so its size is known before the slot count that depends on it.
    struct Bucket_Header
    {
        Bucket* next_bucket = nullptr;     // every bucket, in allocation order
        Bucket* next_available = nullptr;  // only buckets that still have a free slot
        size_t base_index = 0;
        uint32_t bump_cursor = 0;          // slots below this have been handed out at least once
        uint32_t free_head = invalid_slot; // intrusive list through Slot::next_free
        uint32_t occupied = 0;
        uint32_t owner_tag = 0;            // catches free(Type*) on a foreign pointer
        bool available = false;
    };

    static constexpr uint32_t bucket_owner_tag = 0x5AB1E10C; // "stable loc"

    static constexpr size_t bucket_bytes_for(size_t slot_bytes)
    {
        size_t minimum = sizeof(Bucket_Header) + minimum_slots_per_bucket * slot_bytes;
        return round_up_to_power_of_two(Bucket_Bytes > minimum ? Bucket_Bytes : minimum);
    }

    // decided on the plain slot, then the slot is chosen to match. Adding the owner
    // pointer only ever grows a bucket, so it can never turn a maskable one unmaskable.
    static constexpr bool bucket_is_maskable = bucket_bytes_for(sizeof(Slot_Plain)) <= maximum_bucket_alignment;

    using Slot = std::conditional_t<bucket_is_maskable, Slot_Plain, Slot_With_Owner>;

    static constexpr size_t bucket_bytes = bucket_bytes_for(sizeof(Slot));
    static constexpr size_t slots_per_bucket = (bucket_bytes - sizeof(Bucket_Header)) / sizeof(Slot);

    // when maskable, alignment == size is what makes the address mask land on the
    // bucket. Otherwise this is just the natural alignment and changes nothing.
    static constexpr size_t bucket_alignment =
        bucket_is_maskable ? bucket_bytes
                           : (alignof(Slot) > alignof(Bucket_Header) ? alignof(Slot) : alignof(Bucket_Header));

    struct alignas(bucket_alignment) Bucket : Bucket_Header
    {
        Slot slots[slots_per_bucket];
    };

    static_assert(slots_per_bucket >= minimum_slots_per_bucket, "bucket sizing dropped below the slot floor");
    static_assert(sizeof(Bucket) <= bucket_bytes, "a bucket must fit its own size class");
    static_assert(!bucket_is_maskable || sizeof(Bucket) == bucket_bytes,
                  "a maskable bucket must exactly fill its alignment, or the mask is wrong");

    Bucket* first_bucket = nullptr;
    Bucket* available_head = nullptr;
    Vector<Bucket*> bucket_table; // bucket_ordinal -> bucket, for O(1) index lookup
    size_t occupied = 0;

    Stable_Array() = default;
    ~Stable_Array() { clear(); }
    // explicitly delete other operators. add them when we need them.
    Stable_Array(const Stable_Array&) = delete;
    Stable_Array& operator=(const Stable_Array&) = delete;
    Stable_Array(Stable_Array&&) = delete;
    Stable_Array& operator=(Stable_Array&&) = delete;

    void clear()
    {
        Bucket* bucket = first_bucket;
        while (bucket) {
            Bucket* next = bucket->next_bucket;
            delete bucket;
            bucket = next;
        }
        first_bucket = nullptr;
        available_head = nullptr;
        bucket_table.reset();
        occupied = 0;
    }

    size_t bucket_count() const { return bucket_table.occupied; }
    size_t capacity() const { return bucket_count() * slots_per_bucket; }

    Bucket* find_bucket(size_t index) const
    {
        size_t bucket_ordinal = index / slots_per_bucket;
        if (bucket_ordinal >= bucket_table.occupied)
            return nullptr;
        return bucket_table[bucket_ordinal];
    }

    Slot* find_slot(size_t index) const
    {
        Bucket* bucket = find_bucket(index);
        if (!bucket)
            return nullptr;
        return &bucket->slots[index - bucket->base_index];
    }

    bool valid(size_t index) const
    {
        Slot* slot = find_slot(index);
        return slot && slot->valid;
    }

    // nullptr when the index was never allocated or has since been freed.
    Type* get(size_t index) const
    {
        Slot* slot = find_slot(index);
        if (!slot || !slot->valid)
            return nullptr;
        return &slot->value;
    }

    size_t allocate(const Type& value)
    {
        Type* element = nullptr;
        return allocate(value, element);
    }

    // element is set to the (stable) address of the new value.
    size_t allocate(const Type& value, Type*& element)
    {
        // full buckets get dropped off the available list here rather than in free(),
        // which keeps free() from having to unlink from a singly linked list.
        while (available_head && !has_space(available_head)) {
            Bucket* full_bucket = available_head;
            available_head = full_bucket->next_available;
            full_bucket->available = false;
            full_bucket->next_available = nullptr;
        }

        Bucket* bucket = available_head ? available_head : append_bucket();

        uint32_t slot_index;
        if (bucket->free_head != invalid_slot) {
            slot_index = bucket->free_head;
            bucket->free_head = bucket->slots[slot_index].next_free;
        } else {
            slot_index = bucket->bump_cursor++;
        }

        Slot& slot = bucket->slots[slot_index];
        slot.value = value;
        slot.next_free = invalid_slot;
        slot.valid = true;
        bucket->occupied += 1;
        occupied += 1;

        element = &slot.value;
        return bucket->base_index + slot_index;
    }

    void free(size_t index)
    {
        Bucket* bucket = find_bucket(index);
        assert(bucket && "Stable_Array::free on an index that was never allocated");
        if (!bucket)
            return;

        free_slot(bucket, static_cast<uint32_t>(index - bucket->base_index));
    }

    // the bucket is recovered from the address itself, so this costs a mask and a
    // subtract - no index, no table lookup. element must have come from this container.
    void free(Type* element)
    {
        assert(element && "Stable_Array::free on a null pointer");
        if (!element)
            return;

        Bucket* bucket = owning_bucket(element);
        assert(bucket->owner_tag == bucket_owner_tag && "Stable_Array::free on a pointer this container never handed out");

        size_t byte_offset = reinterpret_cast<uintptr_t>(element) - reinterpret_cast<uintptr_t>(&bucket->slots[0]);
        uint32_t slot_index = static_cast<uint32_t>(byte_offset / sizeof(Slot));
        assert(slot_index < slots_per_bucket && &bucket->slots[slot_index].value == element &&
               "Stable_Array::free on a pointer that is not the start of an element");

        free_slot(bucket, slot_index);
    }

    // which bucket an element lives in. Only valid for elements of this container.
    static Bucket* owning_bucket(Type* element)
    {
        if constexpr (bucket_is_maskable) {
            uintptr_t address = reinterpret_cast<uintptr_t>(element);
            return reinterpret_cast<Bucket*>(address & ~(static_cast<uintptr_t>(bucket_bytes) - 1));
        } else {
            return reinterpret_cast<Slot*>(element)->owner;
        }
    }

    Type& operator[](size_t index)
    {
        Slot* slot = find_slot(index);
        assert(slot && slot->valid && "Stable_Array::operator[] on a dead index");
        return slot->value;
    }

    struct iterator
    {
        Bucket* bucket = nullptr;
        uint32_t slot_index = 0;

        void skip_invalid()
        {
            while (bucket) {
                if (slot_index >= slots_per_bucket) {
                    bucket = bucket->next_bucket;
                    slot_index = 0;
                    continue;
                }
                if (bucket->slots[slot_index].valid)
                    return;
                ++slot_index;
            }
            slot_index = 0;
        }

        iterator(Bucket* bucket_, uint32_t slot_index_) : bucket(bucket_), slot_index(slot_index_) { skip_invalid(); }

        Type& operator*() const { return bucket->slots[slot_index].value; }
        size_t index() const { return bucket->base_index + slot_index; }

        iterator& operator++()
        {
            ++slot_index;
            skip_invalid();
            return *this;
        }

        bool operator!=(iterator const& other) const
        {
            return bucket != other.bucket || slot_index != other.slot_index;
        }
    };

    auto begin() { return iterator(first_bucket, 0); }
    auto end() { return iterator(nullptr, 0); }

  private:
    void free_slot(Bucket* bucket, uint32_t slot_index)
    {
        Slot& slot = bucket->slots[slot_index];
        assert(slot.valid && "Stable_Array::free on an already freed slot");
        if (!slot.valid)
            return;

        slot.valid = false;
        slot.value = Type{}; // so a stale index can't observe the old value
        slot.next_free = bucket->free_head;
        bucket->free_head = slot_index;
        bucket->occupied -= 1;
        occupied -= 1;

        if (!bucket->available) {
            bucket->available = true;
            bucket->next_available = available_head;
            available_head = bucket;
        }
    }

    bool has_space(const Bucket* bucket) const
    {
        return bucket->free_head != invalid_slot || bucket->bump_cursor < slots_per_bucket;
    }

    Bucket* append_bucket()
    {
        // when maskable, Bucket is over-aligned and this picks the aligned operator new
        // (and the matching delete in clear()).
        Bucket* bucket = new Bucket();
        bucket->base_index = bucket_table.occupied * slots_per_bucket;
        bucket->owner_tag = bucket_owner_tag;

        if constexpr (!bucket_is_maskable) {
            for (size_t slot_index = 0; slot_index < slots_per_bucket; ++slot_index)
                bucket->slots[slot_index].owner = bucket;
        }

        if (bucket_table.occupied > 0)
            bucket_table[bucket_table.occupied - 1]->next_bucket = bucket;
        else
            first_bucket = bucket;
        bucket_table.push_back(bucket);

        bucket->available = true;
        bucket->next_available = available_head;
        available_head = bucket;
        return bucket;
    }
};