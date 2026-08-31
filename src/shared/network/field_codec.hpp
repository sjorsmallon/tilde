#pragma once

// field_codec -- one field of one struct, on the wire. The primitive half of
// every schema family's serialization, and the only place a field_type_t
// becomes bits.
//
// It knows a field_info_t and a byte offset, and nothing about what encloses
// them: entities pass a composed leaf offset (a component's own offset already
// added in), the event families pass the flat offset their tables carry. The
// set-level grammar -- which entities exist, which changed, which are gone --
// stays in entity_snapshot.hpp; the delta MASK stays in entity_serialization.
//
// The encodings, once, since two hand-written codecs used to agree on them by
// hand:
//
//   f32, v3, v4        write_coord, quantized to ~1/32
//   f64, u64           write_var_uint64
//   bool               1 bit
//   u8/u16/u32         write_var_uint
//   enum, asset        write_var_uint over the id, range-checked on read
//   i8/i16/i32, v4i    write_var_int
//   i64                write_var_int64
//   string<N>          a length byte, then that many characters
//   quat               four RAW 32-bit floats
//
// A field needing full float precision does not belong on this wire, and `quat`
// is the ONE exception with the reason written at its arm: its components live
// in [-1, 1], so write_coord's 5-bit fraction is 3.6 degrees of angular error
// and a value too far off unit for to_mat4 to be a rotation. Compressing it
// properly is a smallest-three encoding, which rotation_def.md §5 defers to
// whenever snapshot delta compression is the thing being worked on.

#include "../reflection.hpp"
#include "bitstream.hpp"
#include "quantization.hpp"

#include <cstdint>

namespace network
{

// Writes the field at `base + offset`. FIELD_TYPE_COMPONENT is not a leaf and
// an invalid tag is a generator bug; both fatal_error rather than write
// nothing, since a silent skip desyncs every field after it.
void write_field(Bit_Writer& writer, const uint8_t* base, const field_info_t& field,
                 uint32_t offset);

// Reads one field back into `base + offset`.
//
// False if the value is not representable in this build's tables -- an enum
// value or asset id outside the declared set. Nothing else can fail: every
// other type accepts its whole bit pattern.
//
// Why reject rather than clamp: the receiving side dispatches on these. An
// out-of-range enum reaches an exhaustive switch with no matching case, and an
// out-of-range asset id indexes a manifest -- so accepting one is choosing a
// wrong-but-plausible value over a loud stop. A rejected field also leaves the
// read position mid-record, so the caller has nothing to salvage and drops the
// packet whole.
[[nodiscard]] bool read_field(Bit_Reader& reader, uint8_t* base, const field_info_t& field,
                              uint32_t offset);

} // namespace network
