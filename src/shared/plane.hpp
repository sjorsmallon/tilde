#pragma once
#include "linalg.hpp"
#include <cmath>
#include <print>

using namespace linalg;

struct Plane
{
  vec3f point;
  vec3f normal;
};

enum class Partition_Result
{
  BACK,
  FRONT,
  STRADDLING
};

inline Plane to_plane(vec3f& v0, vec3f& v1, vec3f& v2)
{
  vec3f e0 = normalize(v1 - v0);
  vec3f e1 = normalize(v2 - v0);
  vec3f face_normal_at_v0 = normalize(cross(e0, e1));

  return Plane{.point = v0, .normal = face_normal_at_v0};
}

// --- Face identity -----------------------------------------------------------
//
// A face's identity is its PLANE, never its index: faces are derived from the
// canonical vertex set and rebuilt on every edit. Two things key off that -- a
// face's SURFACE (its material, its grid) and its lightmap CHART -- and they
// have to agree, or a face takes its material from one rule and its lighting
// from another. This is that rule, written once.

// The normal test is the strict one: a face that has ROTATED past this is a
// different face, and inheriting across that is worse than falling back to the
// default. The distance test is loose, because a face slid along its own normal
// by a drag is still that face.
constexpr float FACE_KEY_NORMAL_DOT = 0.985f; // about 10 degrees
constexpr float FACE_KEY_MAX_DISTANCE = 64.f;

[[nodiscard]] inline float plane_distance(const Plane& plane)
{
  return dot(plane.normal, plane.point);
}

// How well a stored key matches a derived face. `matched` false means it is not
// a candidate at all, and the two scores are only meaningful when it is true.
struct face_key_match_t
{
  bool matched = false;
  float normal_dot = 0.f;
  float distance_error = 0.f;

  // Normal first, distance second: two parallel faces of a brush are told apart
  // by distance, but a face that rotated is a worse match than one that slid
  // however far it slid.
  [[nodiscard]] bool is_better_than(const face_key_match_t& best) const
  {
    if (!best.matched)
      return true;
    if (normal_dot > best.normal_dot + 1e-4f)
      return true;
    return std::abs(normal_dot - best.normal_dot) <= 1e-4f &&
           distance_error < best.distance_error;
  }
};

[[nodiscard]] inline face_key_match_t match_face_key(const vec3f& key_normal,
                                                     float key_distance,
                                                     const Plane& face)
{
  face_key_match_t match;

  match.normal_dot = dot(face.normal, key_normal);
  if (match.normal_dot < FACE_KEY_NORMAL_DOT)
    return match;

  match.distance_error = std::abs(plane_distance(face) - key_distance);
  if (match.distance_error > FACE_KEY_MAX_DISTANCE)
    return match;

  match.matched = true;
  return match;
}

[[nodiscard]] inline face_key_match_t match_face_key(const Plane& key, const Plane& face)
{
  return match_face_key(key.normal, plane_distance(key), face);
}

// // because #derive[display, debug] is too fucking hard I guess.
// template <> struct std::formatter<Plane> : std::formatter<std::string>
// {
//   // Format the vec3f as a string
//   auto format(const Plane &plane, std::format_context &ctx) const
//   {
//     return std::format_to(ctx.out(),
//                           "Plane: \n\tposition: [{}]\n\t normal: [{}]\n",
//                           plane.point, plane.normal);
//   }
// };