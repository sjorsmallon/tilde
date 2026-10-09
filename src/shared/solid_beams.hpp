#pragma once

// A spot's beam CARVED: the reveal cone's pyramid AROUND the round cone beam.frag draws
// (planes_of_reveal_cone, the same planes that cut the light's shadow volumes to its beam, so the carve
// leaves no sliver between two polygons of one cone), ending on the range sphere's tangent planes, less
// the shadow volumes the spot's own light throws: a wall ends it, a crate in it leaves a hole behind
// itself. The lit part of a pyramid less one volume is not convex, so it is carved into disjoint convex
// pieces, each the pyramid plus the planes that cut it. It follows the light, so a light that rides a
// mover carries its beam.
//
// Every beam is DRAWN from its carve: beam.frag clips its round cone chord by each piece's CUT planes (the
// planes a split added, never the pyramid's: the cone is inside the pyramid) and sums the lengths, so what
// is drawn is the cone less the volumes (spot_beam_plan.md ss3). try_find_beam_carve hands the renderer
// those planes; the client carves every beam spot, the server only the solid ones (beam_carve_scope_t).
//
// A spot with `solid_beam` set is the same pieces made SOLID, cut as a mover whose two poses are equal,
// as a landed platform is: the pyramid less the volumes contains the cone less the volumes, so what is
// drawn is solid everywhere. A beam casts no shadow and stops none (collect_shadow_volumes skips it). It
// carries nobody and crushes nobody.

#include "movers.hpp"
#include "shadow_volume.hpp"
#include "span.hpp"

#include <optional>
#include <vector>

namespace shared
{

struct Entity_System;

inline constexpr float    MAX_SOLID_BEAM_HALF_ANGLE_DEGREES = 80.f;
// Past this many pieces a beam keeps what it has, skips the volumes left and logs the spot once: a
// level that puts this many casters in one beam is a level to simplify.
inline constexpr uint32_t MAX_SOLID_BEAM_PIECES = 64;
// Carves kept per spot: a reconciliation replay re-cuts the last few ticks and a light that moves has a
// pose per tick, so each of those ticks' carves is found again rather than redone.
inline constexpr uint32_t SOLID_BEAM_CACHE_ENTRIES_PER_SPOT = 8;

// The carve's pieces in flat arrays: a piece is a run of faces, a face a run of corners, and a piece
// carries the run of cut planes it inherited. A split appends its halves and retires what it split;
// nothing is freed until the next carve clears the arena, whose capacity stays.
struct solid_beam_face_t
{
  Plane    plane;
  uint32_t corner_first = 0;
  uint32_t corner_count = 0;
};

struct solid_beam_piece_t
{
  uint32_t      face_first = 0;
  uint32_t      face_count = 0;
  uint32_t      cut_first  = 0;
  uint32_t      cut_count  = 0;
  aabb_bounds_t bounds     = {};
  bool          live       = false;
};

struct solid_beam_arena_t
{
  std::vector<solid_beam_face_t>  faces;
  std::vector<linalg::vec3>       corners;
  std::vector<Plane>              cut_planes;
  std::vector<solid_beam_piece_t> pieces;
};

// The uncarved beam of one range and half angle, built once with its apex at the origin along +X and
// moved to each pose it is carved at: the pyramid's shape is the numbers', never the pose's.
struct solid_beam_canonical_pyramid_t
{
  float              range      = 0.f;
  float              half_angle = 0.f;
  solid_beam_arena_t arena;
};

// One spot's carve, kept with what it was carved from: the pose, the numbers and the volumes its light
// threw, nearest caster first. A tick whose inputs are equal reuses the pieces. `stamp` orders the
// entries: the newest is what the renderer draws and the oldest is what a miss overwrites.
struct solid_beam_cache_entry_t
{
  entity_uid_t                   spot       = null_entity_uid;
  uint64_t                       stamp      = 0;
  path_pose_t                    pose       = {};
  float                          range      = 0.f;
  float                          half_angle = 0.f;
  std::vector<shadow_volume_t>   volumes;
  std::vector<collision_piece_t> pieces;
  // Piece p's cut planes are drawn_cut_planes[drawn_cut_first[p], drawn_cut_first[p + 1]).
  std::vector<Plane>             drawn_cut_planes;
  std::vector<uint32_t>          drawn_cut_first;
};

struct solid_beam_carve_scratch_t
{
  solid_beam_arena_t    arena;
  std::vector<float>    heights;
  std::vector<linalg::vec3> cap_points;
  std::vector<uint32_t> live_pieces;
};

struct solid_beam_cache_t
{
  std::vector<solid_beam_cache_entry_t>       entries;
  std::vector<solid_beam_canonical_pyramid_t> pyramids;
  solid_beam_carve_scratch_t                  scratch;
  uint64_t                                    next_stamp = 1;
};

// What beam.frag cuts a beam's cone chord by: piece p's planes are cut_planes[piece_first[p],
// piece_first[p + 1]), piece_first one longer than the piece count.
struct beam_carve_t
{
  Span<const Plane>    cut_planes;
  Span<const uint32_t> piece_first;
};

// Which spots are carved: the server needs only the solid ones, the client draws every beam from its carve.
enum class beam_carve_scope_t : uint8_t
{
  Solid,
  Every_Beam
};

// APPENDS the solid beams as movers, after collect_movers and collect_shadow_volumes: a riding light is
// read at the pose of the movers already in `out`, and the volumes are those the beam's own light threw
// this tick. Under Every_Beam a `beam` spot is carved too and pushes no mover.
void collect_solid_beams(const Entity_System& system, const mover_rests_t& rests,
                         Span<const shadow_volume_t> shadow_volumes, beam_carve_scope_t scope,
                         solid_beam_cache_t& cache, std::vector<mover_t>& out);

// The newest carve of `spot`, or nothing when it has not been carved (an editor, a spot switched off).
[[nodiscard]] std::optional<beam_carve_t> try_find_beam_carve(const solid_beam_cache_t& cache, entity_uid_t spot);

} // namespace shared
