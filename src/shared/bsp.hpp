#pragma once
#include "linalg.hpp"
#include "plane.hpp"
#include "vertex.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cfloat> // FLT_MAX
#include <cmath>
#include <functional>
#include <numeric> // iota..

using namespace linalg;

struct AABB
{
  vec3f min;
  vec3f max;
};

inline AABB aabb_from_triangle(const vec3f &v0, const vec3f &v1,
                               const vec3f &v2)
{
  vec3f min = {std::min({v0.x, v1.x, v2.x}), std::min({v0.y, v1.y, v2.y}),
               std::min({v0.z, v1.z, v2.z})};
  vec3f max = {std::max({v0.x, v1.x, v2.x}), std::max({v0.y, v1.y, v2.y}),
               std::max({v0.z, v1.z, v2.z})};
  return AABB{min, max};
}

// move to math
inline size_t abs(size_t a, size_t b) { return (a > b) ? (a - b) : (b - a); }

inline vec3f compute_triangle_normal(const vec3f &p0, const vec3f &p1,
                                     const vec3f &p2)
{
  vec3f edge1 = p1 - p0;
  vec3f edge2 = p2 - p0;
  return normalize(cross(edge1, edge2));
}

// Free function to classify the position of the AABB relative to a plane
inline Partition_Result classify_aabb_against_plane(const AABB &aabb,
                                                    const vec3f &normal,
                                                    const vec3f &point)
{
  float d_min = dot(normal, aabb.min - point);
  float d_max = dot(normal, aabb.max - point);

  if (d_min > 0 && d_max > 0)
  {
    return Partition_Result::FRONT; // Entirely in front
  }
  else if (d_min < 0 && d_max < 0)
  {
    return Partition_Result::BACK; // Entirely behind
  }
  else
  {
    return Partition_Result::STRADDLING; // Straddling the plane
  }
}

struct BSP
{
  BSP *front = nullptr;
  BSP *back = nullptr;
  uint64_t face_idx; // keep the face data in a contiguous array outside of the
                     // bsp, but refer to the "face_idx" so we know what we are
                     // talking about.
};

// partition result is "where do we put this face?"
inline Partition_Result get_partition_result(Plane &plane, const vec3f &v0,
                                             const vec3f &v1, const vec3f &v2)
{
  float d0 = dot(plane.normal, v0 - plane.point);
  float d1 = dot(plane.normal, v1 - plane.point);
  float d2 = dot(plane.normal, v2 - plane.point);

  // check the signs of the distances
  bool is_v0_front = (d0 > 0.f);
  bool is_v1_front = (d1 > 0.f);
  bool is_v2_front = (d2 > 0.f);

  // all vertices are in front of the plane
  if (is_v0_front && is_v1_front && is_v2_front)
  {
    return Partition_Result::FRONT;
  }
  // All vertices are behind the plane
  else if (!is_v0_front && !is_v1_front && !is_v2_front)
  {
    return Partition_Result::BACK;
  }
  else
  {
    return Partition_Result::STRADDLING; // The face straddles the plane
  }
}

// move to math. Thank you, Christer Ericcson. I owe you one.
inline bool is_point_in_triangle(const vec3f &point, const vec3f &v0,
                                 const vec3f &v1, const vec3f &v2)
{
  // translate point and triangle so that point lies at origin
  vec3f a = v0 - point;
  vec3f b = v1 - point;
  vec3f c = v2 - point;

  // compute normal vectors for triangles pab and pbc.
  vec3f u = cross(b, c);
  vec3f v = cross(c, a);

  // make sure they are ponting in the same direction.
  if (dot(u, v) < 0.0f)
    return false;

  // compute normal vector for triangle pca
  vec3f w = cross(a, b);
  // make sure it points in the same direction as the first two
  if (dot(u, w) < 0.0f)
    return false;
  // otehrwise p must be in (or on) the triangle

  return true;
}

// move to math
inline float point_to_edge_distance(const vec3f &point, const vec3f &edge_start,
                                    const vec3f &edge_end)
{
  vec3f edge = edge_end - edge_start;        // Vector along the edge
  vec3f point_to_start = point - edge_start; // Vector from edge start to point

  // why the length squared?
  // Calculate the projection of the point onto the edge (normalized)
  float edge_length_squared = length_squared(edge);

  // Avoid division by zero if edge_length is zero
  if (edge_length_squared == 0.0f)
  {
    return distance_between(
        point, edge_start); // Edge is a point, return distance to start
  }

  // Project the point onto the edge
  float t = dot(point_to_start, edge) / edge_length_squared;

  // Clamp the projection value t to the range [0, 1]
  t = std::max(0.0f, std::min(1.0f, t));

  // Find the closest point on the edge to the point
  vec3f closest_point = edge_start + edge * t;

  // Return the distance from the point to the closest point on the edge
  return distance_between(point, closest_point);
}

// move to math
// returns the distance to the closest point of any particular triangle.
inline float point_to_triangle_distance(const vec3f &p, const vec3f &v0,
                                        const vec3f &v1, const vec3f &v2)
{
  // get the normal of the triangle plane
  vec3f edge0 = v1 - v0;
  vec3f edge1 = v2 - v0;
  vec3f normal = cross(edge0, edge1);
  normal = normalize(normal);

  // Project the point onto the triangle's plane (draw a line from the point to
  // perpendicularly to the triangle. find that collision.)
  vec3f v0_to_p = p - v0;
  float distance_to_plane = dot(v0_to_p, normal);

  // Step 3: Get the point projection on the plane
  vec3f projected_point = p - normal * distance_to_plane;

  // Step 4: Check if the projected point is inside the triangle
  bool inside = is_point_in_triangle(projected_point, v0, v1, v2);
  if (inside)
  {
    return fabs(distance_to_plane); // If inside the triangle, this is the
                                    // closest distance
  }

  // Step 5: Otherwise, calculate the minimum distance to the triangle's edges
  // or vertices
  float dist_to_v0 = distance_between(p, v0);
  float dist_to_v1 = distance_between(p, v1);
  float dist_to_v2 = distance_between(p, v2);

  float dist_to_edge0 = point_to_edge_distance(p, v0, v1);
  float dist_to_edge1 = point_to_edge_distance(p, v1, v2);
  float dist_to_edge2 = point_to_edge_distance(p, v2, v0);

  return std::min({dist_to_v0, dist_to_v1, dist_to_v2, dist_to_edge0,
                   dist_to_edge1, dist_to_edge2});
}

//@Memory: we never free this
//@Speed: Quadratic behavior! for all planes, for all other planes. or is it
// log?
// global_buffer is not const since it can be the case we add faces based on a
// STRADDLING face.
inline BSP *build_bsp(std::vector<vertex_xnc> &vertices)
{
  // Construct the initial list of face indices
  assert(vertices.size() % 3 == 0);
  std::vector<uint64_t> face_indices(vertices.size() / 3);
  auto index_of_start_of_face_in_vertices = 0;
  for (auto &face_idx : face_indices)
  {
    face_idx = index_of_start_of_face_in_vertices;
    index_of_start_of_face_in_vertices += 3;
  }

  // Recursive lambda for building the BSP tree
  std::function<BSP *(const std::vector<uint64_t> &)> build_bsp_recursive =
      [&](const std::vector<uint64_t> &indices) -> BSP *
  {
    if (indices.empty())
    {
      return nullptr; // No faces to partition
    }

    BSP *bsp = new BSP{};

    std::vector<uint64_t> best_front_indices;
    std::vector<uint64_t> best_back_indices;
    bool initial = true;

    for (uint64_t candidate_idx : indices)
    {
      std::vector<uint64_t> front_indices;
      std::vector<uint64_t> back_indices;

      // Extract candidate face vertices
      vec3f &v0 = vertices[candidate_idx].position;
      vec3f &v1 = vertices[candidate_idx + 1].position;
      vec3f &v2 = vertices[candidate_idx + 2].position;

      Plane plane = to_plane(v0, v1, v2);

      // Partition other faces
      for (uint64_t other_idx : indices)
      {
        if (other_idx == candidate_idx)
          continue;

        vec3f &other_v0 = vertices[other_idx].position;
        vec3f &other_v1 = vertices[other_idx + 1].position;
        vec3f &other_v2 = vertices[other_idx + 2].position;

        Partition_Result partition_result =
            get_partition_result(plane, other_v0, other_v1, other_v2);

        if (partition_result == Partition_Result::FRONT)
        {
          front_indices.push_back(other_idx);
        }
        if (partition_result == Partition_Result::BACK)
        {
          back_indices.push_back(other_idx);
        }
        if (partition_result == Partition_Result::STRADDLING)
        {
          front_indices.push_back(other_idx);
          back_indices.push_back(other_idx);
        }
      }

      if (initial)
      {
        best_front_indices = std::move(front_indices);
        best_back_indices = std::move(back_indices);
        bsp->face_idx = candidate_idx;
        initial = false;
      }
      else
      {
        size_t absolute_delta = abs(
            static_cast<int64_t>(front_indices.size() - back_indices.size()));
        if (absolute_delta <
            abs(static_cast<int64_t>(best_front_indices.size() -
                                     best_back_indices.size())))
        {
          best_front_indices = std::move(front_indices);
          best_back_indices = std::move(back_indices);
          bsp->face_idx = candidate_idx;
        }
      }
    }

    bsp->front = build_bsp_recursive(best_front_indices);
    bsp->back = build_bsp_recursive(best_back_indices);

    return bsp;
  };

  // Start the recursion with the full set of indices
  return build_bsp_recursive(face_indices);
}

inline float calculate_penetration_depth(const vec3f &point,
                                         const vec3f &plane_normal,
                                         const vec3f &point_on_plane)
{
  // Calculate signed distance
  return dot(point - point_on_plane, plane_normal);
}

inline float calculate_penetration_depth_triangle(const vec3f &aabb_center,
                                                  const vec3f &triangle_normal,
                                                  const vec3f &triangle_point)
{
  return dot(aabb_center - triangle_point, triangle_normal);
}

inline float calculate_max_penetration_depth(const AABB &aabb,
                                             const vec3f &triangle_vertex0,
                                             const vec3f &triangle_vertex1,
                                             const vec3f &triangle_vertex2)
{
  std::vector<vec3f> aabb_vertices = {
      vec3f{.x = aabb.min.x, .y = aabb.min.y, .z = aabb.min.z}, // min corner
      vec3f{.x = aabb.min.x,
            .y = aabb.min.y,
            .z = aabb.max.z}, // front bottom left
      vec3f{.x = aabb.min.x,
            .y = aabb.max.y,
            .z = aabb.min.z}, // back bottom left
      vec3f{.x = aabb.min.x, .y = aabb.max.y, .z = aabb.max.z}, // back top left
      vec3f{.x = aabb.max.x, .y = aabb.min.y, .z = aabb.min.z}, // min corner
      vec3f{.x = aabb.max.x,
            .y = aabb.min.y,
            .z = aabb.max.z},                    // front bottom right
      vec3f{aabb.max.x, aabb.max.y, aabb.min.z}, // back bottom right
      vec3f{aabb.max.x, aabb.max.y, aabb.max.z}  // back top right
  };

  vec3f triangle_normal = compute_triangle_normal(
      triangle_vertex0, triangle_vertex1, triangle_vertex2);
  vec3f triangle_point = triangle_vertex0; // Any vertex on the triangle can be
                                           // used as a point on the plane.

  float max_penetration_depth = std::numeric_limits<float>::lowest();

  // Loop through each vertex of the AABB
  for (const vec3f &vertex : aabb_vertices)
  {
    float penetration_depth = calculate_penetration_depth_triangle(
        vertex, triangle_normal, triangle_point);

    if (penetration_depth > max_penetration_depth)
    {
      max_penetration_depth = penetration_depth;
    }
  }

  return max_penetration_depth;
}

// Utility to get min and max projection of a triangle on an axis
inline void project_triangle_on_axis(const vec3f &p0, const vec3f &p1,
                                     const vec3f &p2, const vec3f &axis,
                                     float &min, float &max)
{
  min = max = dot(p0, axis);
  float proj1 = dot(p1, axis);
  float proj2 = dot(p2, axis);
  min = std::min({min, proj1, proj2});
  max = std::max({max, proj1, proj2});
}

// Utility to get min and max projection of an AABB on an axis
inline void project_aabb_on_axis(const AABB &aabb, const vec3f &axis,
                                 float &min, float &max)
{
  std::array<vec3f, 8> corners = {
      vec3f{.x = aabb.min.x, .y = aabb.min.y, .z = aabb.min.z},
      vec3f{.x = aabb.min.x, .y = aabb.min.y, .z = aabb.max.z},
      vec3f{.x = aabb.min.x, .y = aabb.max.y, .z = aabb.min.z},
      vec3f{.x = aabb.min.x, .y = aabb.max.y, .z = aabb.max.z},
      vec3f{.x = aabb.max.x, .y = aabb.min.y, .z = aabb.min.z},
      vec3f{.x = aabb.max.x, .y = aabb.min.y, .z = aabb.max.z},
      vec3f{.x = aabb.max.x, .y = aabb.max.y, .z = aabb.min.z},
      vec3f{.x = aabb.max.x, .y = aabb.max.y, .z = aabb.max.z}};

  min = max = dot(corners[0], axis);
  for (const auto &corner : corners)
  {
    float proj = dot(corner, axis);
    min = std::min(min, proj);
    max = std::max(max, proj);
  }
}

inline bool triangle_intersects_aabb(const vec3f &p0, const vec3f &p1,
                                     const vec3f &p2, const AABB &aabb)
{

  std::array<vec3f, 3> aabb_axes{vec3f{.x = 1.f, .y = 0, .z = 0},
                                 vec3f{.x = 0, .y = 1.f, .z = 0},
                                 vec3f{.x = 0, .y = 0, .z = 1.f}};

  // Triangle edges
  vec3f edge1 = p1 - p0;
  vec3f edge2 = p2 - p1;
  vec3f edge3 = p0 - p2;

  // 1. Test triangle normal as an axis
  vec3f triangle_normal = normalize(cross(edge1, edge2));
  float tri_min, tri_max, aabb_min, aabb_max;

  project_triangle_on_axis(p0, p1, p2, triangle_normal, tri_min, tri_max);
  project_aabb_on_axis(aabb, triangle_normal, aabb_min, aabb_max);

  if (tri_max < aabb_min || aabb_max < tri_min)
    return false;

  // 2. Test the AABB face normals
  for (const auto &axis : aabb_axes)
  {
    project_triangle_on_axis(p0, p1, p2, axis, tri_min, tri_max);
    project_aabb_on_axis(aabb, axis, aabb_min, aabb_max);

    if (tri_max < aabb_min || aabb_max < tri_min)
      return false;
  }

  // 3. Test cross products of triangle edges and AABB axes
  std::array<vec3f, 9> cross_axes = {
      cross(edge1, aabb_axes[0]), cross(edge1, aabb_axes[1]),
      cross(edge1, aabb_axes[2]), cross(edge2, aabb_axes[0]),
      cross(edge2, aabb_axes[1]), cross(edge2, aabb_axes[2]),
      cross(edge3, aabb_axes[0]), cross(edge3, aabb_axes[1]),
      cross(edge3, aabb_axes[2])};

  for (const auto &axis : cross_axes)
  {
    if (length_squared(axis) < 1e-6)
      continue; // Skip near-zero axis
    project_triangle_on_axis(p0, p1, p2, axis, tri_min, tri_max);
    project_aabb_on_axis(aabb, axis, aabb_min, aabb_max);

    if (tri_max < aabb_min || aabb_max < tri_min)
      return false;
  }

  // No separating axis found; they intersect
  return true;
}

// FIXME:  provide world_up instead of hardcoding.
//  return ground_faces, ceiling_faces, colliding_faces)
inline std::vector<size_t>
bsp_trace_AABB(const BSP *bsp, const AABB &aabb,
               const std::vector<vertex_xnc> &all_faces_buffer)
{

  std::vector<size_t> colliding_faces;

  // Recursive lambda for traversing the BSP tree
  std::function<void(const BSP *)> traverse = [&](const BSP *node)
  {
    if (node == nullptr)
      return;

    // Get the three vertices of the triangle from `all_faces_buffer`
    const vec3f &v0 = all_faces_buffer[node->face_idx].position;
    const vec3f &v1 = all_faces_buffer[node->face_idx + 1].position;
    const vec3f &v2 = all_faces_buffer[node->face_idx + 2].position;

    // Compute the face normal
    vec3f normal = compute_triangle_normal(v0, v1, v2);

    // Classify the AABB's position relative to the current plane
    Partition_Result side = classify_aabb_against_plane(aabb, normal, v0);

    if (side == Partition_Result::FRONT)
    {
      traverse(node->front);
    }
    else if (side == Partition_Result::BACK)
    {
      traverse(node->back);
    }
    else if (side == Partition_Result::STRADDLING)
    {
      // Traverse both front and back when straddling
      traverse(node->front);
      traverse(node->back);

      // we are straddling the plane, but are we actually colliding?
      if (triangle_intersects_aabb(v0, v1, v2, aabb))
      {
        colliding_faces.push_back(node->face_idx);
      }
    }
  };

  traverse(bsp);

  return colliding_faces;
}
