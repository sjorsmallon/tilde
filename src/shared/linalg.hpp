#pragma once

#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace linalg
{

template <typename T> struct vec2_t
{
  union
  {
    struct
    {
      T x, y;
    };
    struct
    {
      T u, v;
    };
    T data[2];
  };

  T &operator[](int i) { return data[i]; }
  const T &operator[](int i) const { return data[i]; }
};

template <typename T> struct vec3_t
{
  union
  {
    struct
    {
      T x, y, z;
    };
    struct
    {
      T r, g, b;
    };
    T data[3];
  };

  T &operator[](int i) { return data[i]; }
  const T &operator[](int i) const { return data[i]; }
};

template <typename T> struct vec4_t
{
  union
  {
    struct
    {
      T x, y, z, w;
    };
    struct
    {
      T r, g, b, a;
    };
    T data[4];
  };

  T &operator[](int i) { return data[i]; }
  const T &operator[](int i) const { return data[i]; }
};

using vec2 = vec2_t<float>;
using vec2f = vec2_t<float>;
using vec2i = vec2_t<int32_t>;
using vec3 = vec3_t<float>;
using vec3f = vec3_t<float>;
using vec3i = vec3_t<int32_t>;
using vec4 = vec4_t<float>;
using vec4f = vec4_t<float>;
using vec4i = vec4_t<int32_t>;

// Matrix 3x3 (Column Major)
struct mat3f
{
  vec3 cols[3];

  vec3 &operator[](int i) { return cols[i]; }
  const vec3 &operator[](int i) const { return cols[i]; }

  static mat3f identity() { return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; }
  static mat3f diag(float v) { return {{{v, 0, 0}, {0, v, 0}, {0, 0, v}}}; }
  static mat3f from_cols(const vec3 &c0, const vec3 &c1, const vec3 &c2)
  {
    return {{c0, c1, c2}};
  }
};

// Matrix 4x4 (Column Major)
struct mat4f
{
  vec4 cols[4];

  vec4 &operator[](int i) { return cols[i]; }
  const vec4 &operator[](int i) const { return cols[i]; }

  static mat4f identity()
  {
    return {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  }
  static mat4f diag(float v)
  {
    return {{{v, 0, 0, 0}, {0, v, 0, 0}, {0, 0, v, 0}, {0, 0, 0, v}}};
  }
  static mat4f from_cols(const vec4 &c0, const vec4 &c1, const vec4 &c2,
                         const vec4 &c3)
  {
    return {{c0, c1, c2, c3}};
  }
};

// --- Free Functions ---

// Vec3 Operations
template <typename T>
inline vec3_t<T> operator+(const vec3_t<T> &a, const vec3_t<T> &b)
{
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

template <typename T>
inline vec3_t<T> operator-(const vec3_t<T> &a, const vec3_t<T> &b)
{
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

template <typename T> inline vec3_t<T> operator*(const vec3_t<T> &a, T s)
{
  return {a.x * s, a.y * s, a.z * s};
}

template <typename T> inline vec3_t<T> operator*(T s, const vec3_t<T> &a)
{
  return a * s;
}

template <typename T> inline T dot(const vec3_t<T> &a, const vec3_t<T> &b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float length(const vec3 &v) { return std::sqrt(dot(v, v)); }

inline float euclidean_distance_between(const vec3 &a, const vec3 &b)
{
  return length(a - b);
}

template <typename T> inline vec3_t<T> normalize(const vec3_t<T> &v)
{
  T l = length(v);
  if (l > 1e-6f)
    return v * (1.0f / l);
  return {0, 0, 0};
}


template <typename T>
inline vec3_t<T> cross(const vec3_t<T> &a, const vec3_t<T> &b)
{
  return {
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
  };
}

inline float length_squared(const vec3 &v) { return dot(v, v); }


inline float distance_between(const vec3 &a, const vec3 &b)
{
  return length(a - b);
}

inline vec3 normalize(const vec3 &v)
{
  float l = length(v);
  if (l > 1e-6f)
    return v * (1.0f / l);
  return {0, 0, 0};
}

// Vec4 Operations
template <typename T>
inline vec4_t<T> operator+(const vec4_t<T> &a, const vec4_t<T> &b)
{
  return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

template <typename T> inline vec4_t<T> operator*(const vec4_t<T> &a, T s)
{
  return {a.x * s, a.y * s, a.z * s, a.w * s};
}

// Mat4 Operations
inline vec4 operator*(const mat4f &m, const vec4 &v)
{
  return m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w;
}

inline mat4f operator*(const mat4f &a, const mat4f &b)
{
  mat4f res = {}; // init zero
  for (int i = 0; i < 4; ++i)
  {
    res.cols[i] = a * b.cols[i];
  }
  return res;
}

// Inverse of an AFFINE mat4 -- one whose bottom row is (0,0,0,1). Bone matrices,
// model matrices and view matrices are all of that shape, and the general 4x4
// inverse costs several times as much to compute nothing extra.
//
//   M = [ A t ]        M^-1 = [ A^-1   -A^-1 * t ]
//       [ 0 1 ]               [ 0       1        ]
//
// A singular A (an axis scaled to nothing) has no inverse, and there is no
// sensible value to hand back: identity is a lie that surfaces far downstream as
// a mangled pose rather than as the degenerate matrix it actually is. So this is
// fatal. It runs once per bone, so a recoverable version would also mean a
// stacktrace per bone per frame -- and a bone scaled to zero is a broken asset,
// which is exactly the case fatal_error exists for.
inline mat4f inverse_affine(const mat4f &m)
{
  const vec3 a0 = {m[0].x, m[0].y, m[0].z};
  const vec3 a1 = {m[1].x, m[1].y, m[1].z};
  const vec3 a2 = {m[2].x, m[2].y, m[2].z};
  const vec3 translation = {m[3].x, m[3].y, m[3].z};

  // Cofactors of A, which are also the ROWS of the adjugate.
  const vec3 r0 = cross(a1, a2);
  const vec3 r1 = cross(a2, a0);
  const vec3 r2 = cross(a0, a1);

  const float determinant = dot(a0, r0);
  if (determinant > -1e-12f && determinant < 1e-12f)
    fatal_error("inverse_affine: singular 3x3, determinant {}. Column lengths are ({}, {}, {}) -- "
                "a zero one is the axis scaled to nothing.",
                determinant, length(a0), length(a1), length(a2));

  const float inverse_determinant = 1.0f / determinant;

  // A^-1 = adjugate / det, and the adjugate's rows above become its columns.
  mat4f result = mat4f::identity();
  result[0] = {r0.x * inverse_determinant, r1.x * inverse_determinant,
               r2.x * inverse_determinant, 0.0f};
  result[1] = {r0.y * inverse_determinant, r1.y * inverse_determinant,
               r2.y * inverse_determinant, 0.0f};
  result[2] = {r0.z * inverse_determinant, r1.z * inverse_determinant,
               r2.z * inverse_determinant, 0.0f};

  const vec4 inverse_translation = result * vec4{translation.x, translation.y, translation.z, 0.0f};
  result[3] = {-inverse_translation.x, -inverse_translation.y, -inverse_translation.z, 1.0f};
  return result;
}

// --- Quaternions ---
//
// The rotation half of a `transform_t` (src/shared/animation.hpp). A pose is
// stored as TRS rather than as a matrix for exactly one reason: two rotations
// have to be BLENDED, and lerping matrices shears them. Nothing else in the
// engine needs a quaternion, which is why this is the whole of it.
//
// Storage order is x y z w -- the wire/file order too, so the `.animation`
// channel line reads left to right into this struct.
struct quatf
{
  float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;

  static quatf identity() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
};

inline float dot(const quatf &a, const quatf &b)
{
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline quatf normalize(const quatf &q)
{
  const float l = std::sqrt(dot(q, q));
  if (l < 1e-8f)
    return quatf::identity();
  const float inverse = 1.0f / l;
  return {q.x * inverse, q.y * inverse, q.z * inverse, q.w * inverse};
}

// Hamilton product: `a * b` rotates by b FIRST, then a -- the same reading order
// as matrix multiply, so composing a pose is the same shape either way.
inline quatf operator*(const quatf &a, const quatf &b)
{
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

// Normalized lerp with a HEMISPHERE FIX. q and -q are the same rotation, so
// without the dot-sign flip a blend between two poses that happen to have been
// decomposed onto opposite hemispheres takes the long way round -- a limb
// snapping through the body on its way to a pose 10 degrees away.
//
// slerp is deliberately not here: it is a real per-bone cost (an acos, a sin and
// two divides) and at the weights an animator actually blends at, the angular
// error against nlerp is under a degree. See animation_def.md §5.
inline quatf nlerp(const quatf &from, const quatf &to, float t)
{
  const float sign = dot(from, to) < 0.0f ? -1.0f : 1.0f;
  return normalize(quatf{from.x + (to.x * sign - from.x) * t,
                         from.y + (to.y * sign - from.y) * t,
                         from.z + (to.z * sign - from.z) * t,
                         from.w + (to.w * sign - from.w) * t});
}

// Rotation matrix for a UNIT quaternion, column-major to match mat4f.
inline mat4f to_mat4(const quatf &q)
{
  const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
  const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
  const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

  mat4f result = mat4f::identity();
  result[0]    = {1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz), 2.0f * (xz - wy), 0.0f};
  result[1]    = {2.0f * (xy - wz), 1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx), 0.0f};
  result[2]    = {2.0f * (xz + wy), 2.0f * (yz - wx), 1.0f - 2.0f * (xx + yy), 0.0f};
  return result;
}

// Compose translation, rotation and scale into the local matrix the hierarchy
// walk consumes: T * R * S, so scale applies in the bone's own frame and the
// translation is untouched by it.
inline mat4f compose_transform(const vec3f &translation, const quatf &rotation, const vec3f &scale)
{
  mat4f result = to_mat4(rotation);
  result[0]    = result[0] * scale.x;
  result[1]    = result[1] * scale.y;
  result[2]    = result[2] * scale.z;
  result[3]    = {translation.x, translation.y, translation.z, 1.0f};
  return result;
}

// Math Helpers
constexpr float PI = 3.14159265359f;

inline float to_radians(float degrees) { return degrees * (PI / 180.0f); }

inline float to_degrees(float radians) { return radians * (180.0f / PI); }

// --- Camera and model matrices ---
//
// These four are VULKAN-CONVENTION: clip Y points down and clip Z spans [0, 1],
// so the projections below flip Y and remap Z rather than producing the
// OpenGL-style [-1, 1] every textbook derivation gives. They lived as a
// hand-rolled `mat4_t` inside the renderer until the renderer stopped owning a
// math library; nothing about them is renderer-specific, and picking has to
// agree with the projection a frame was drawn with.

// `fov_y_radians` is the VERTICAL field of view. Right-handed, looking down -Z.
inline mat4f perspective(float fov_y_radians, float aspect, float near_plane, float far_plane)
{
  const float tan_half = std::tan(fov_y_radians * 0.5f);

  mat4f result = {};
  result[0].x = 1.0f / (aspect * tan_half);
  result[1].y = -1.0f / tan_half; // negated: clip Y is down, world Y is up
  result[2].z = far_plane / (near_plane - far_plane);
  result[2].w = -1.0f;
  result[3].z = -(far_plane * near_plane) / (far_plane - near_plane);
  return result;
}

inline mat4f orthographic(float left, float right, float bottom, float top, float near_plane,
                          float far_plane)
{
  mat4f result = mat4f::identity();
  result[0].x = 2.0f / (right - left);
  result[1].y = 2.0f / (bottom - top); // bottom-top, not top-bottom: clip Y is down
  result[2].z = 1.0f / (near_plane - far_plane);
  result[3].x = -(right + left) / (right - left);
  result[3].y = -(bottom + top) / (bottom - top);
  result[3].z = near_plane / (near_plane - far_plane);
  return result;
}

inline mat4f look_at(const vec3f &eye, const vec3f &target, const vec3f &up)
{
  const vec3f forward = normalize(target - eye);
  const vec3f right   = normalize(cross(forward, up));
  const vec3f true_up = cross(right, forward);

  mat4f result = mat4f::identity();
  result[0]    = {right.x, true_up.x, -forward.x, 0.0f};
  result[1]    = {right.y, true_up.y, -forward.y, 0.0f};
  result[2]    = {right.z, true_up.z, -forward.z, 0.0f};
  result[3]    = {-dot(right, eye), -dot(true_up, eye), dot(forward, eye), 1.0f};
  return result;
}

// Rz * Ry * Rx, degrees. Euler survives here because map geometry and the entity
// schema store orientation as three floats; anything with a real rotation to
// interpolate uses `quatf` and `to_mat4` instead.
inline mat4f rotation_from_euler_degrees(const vec3f &euler_degrees)
{
  const float cx = std::cos(to_radians(euler_degrees.x)), sx = std::sin(to_radians(euler_degrees.x));
  const float cy = std::cos(to_radians(euler_degrees.y)), sy = std::sin(to_radians(euler_degrees.y));
  const float cz = std::cos(to_radians(euler_degrees.z)), sz = std::sin(to_radians(euler_degrees.z));

  mat4f result = mat4f::identity();
  result[0]    = {cz * cy, sz * cy, -sy, 0.0f};
  result[1]    = {cz * sy * sx - sz * cx, sz * sy * sx + cz * cx, cy * sx, 0.0f};
  result[2]    = {cz * sy * cx + sz * sx, sz * sy * cx - cz * sx, cy * cx, 0.0f};
  return result;
}

// T * R * S with R from euler degrees -- the euler twin of compose_transform,
// and what a draw call composes its model matrix with.
inline mat4f compose_transform_euler(const vec3f &translation, const vec3f &euler_degrees,
                                     const vec3f &scale)
{
  mat4f result = rotation_from_euler_degrees(euler_degrees);
  result[0]    = result[0] * scale.x;
  result[1]    = result[1] * scale.y;
  result[2]    = result[2] * scale.z;
  result[3]    = {translation.x, translation.y, translation.z, 1.0f};
  return result;
}

// An angle difference folded into (-180, 180]. Yaws are stored unwrapped, so a
// player turning past the 0/360 seam produces a raw difference near 360 that
// reads as "turned almost all the way round" -- which is how a smoothly turning
// model ends up spinning the long way.
inline float wrap_degrees(float degrees)
{
  degrees = std::fmod(degrees + 180.0f, 360.0f);
  if (degrees < 0.0f)
    degrees += 360.0f;
  return degrees - 180.0f;
}

// View angles (DEGREES, the form they take at every boundary -- the proto
// viewangles, Player_Entity::view_angle_*, camera_t) to a forward direction.
// Y-up: yaw sweeps from +X toward +Z, positive pitch looks up.
//
// The result is UNIT LENGTH by construction -- (cos*cos)^2 + sin^2 +
// (sin*cos)^2 == 1 identically -- so callers that need a normalized ray
// (resolve_hitscan) can use it directly without a normalize.
inline vec3f direction_from_angles(float yaw_degrees, float pitch_degrees)
{
  const float yaw_radians   = to_radians(yaw_degrees);
  const float pitch_radians = to_radians(pitch_degrees);

  const float cos_yaw   = std::cos(yaw_radians);
  const float sin_yaw   = std::sin(yaw_radians);
  const float cos_pitch = std::cos(pitch_radians);
  const float sin_pitch = std::sin(pitch_radians);

  return {cos_yaw * cos_pitch, sin_pitch, sin_yaw * cos_pitch};
}

// The same yaw as the euler Y of a MODEL matrix, which is not the same number.
// `rotation_from_euler_degrees` sweeps +X toward -Z; a view yaw sweeps +X toward
// +Z (above). So the conversion is a NEGATION, and passing a yaw straight
// through mirrors the model instead of merely offsetting it -- right at 45
// degrees, backwards everywhere else, which is how it read as "the model does
// not face where the player faces".
//
// There is no constant term because models are exported facing +X, which IS yaw
// 0 (blender_export.py's AXIS_CONVERSION). Anything that draws from a view or
// body yaw goes through here; an euler authored in the editor does not, since it
// is already a model rotation.
inline float model_yaw_from_view_yaw(float yaw_degrees) { return -yaw_degrees; }

template <typename T> inline T mix(T a, T b, float t)
{
  return a * (1.0f - t) + b * t;
}

template <typename T> inline T clamp(T v, T min, T max)
{
  return (v < min) ? min : (v > max) ? max : v;
}

// --- Geometry & Intersection ---

// Helper to separate View from Projection for 3D clipping
// Decoupled from camera_t, takes position and yaw/pitch in degrees
inline vec3 world_to_view(const vec3 &p, const vec3 &cam_pos, float cam_yaw_deg,
                          float cam_pitch_deg)
{
  float x = p.x - cam_pos.x;
  float y = p.y - cam_pos.y;
  float z = p.z - cam_pos.z;

  float camYaw = to_radians(cam_yaw_deg);
  float camPitch = to_radians(cam_pitch_deg);

  // Yaw Rotation (align +X to -Z)
  // Original code: float vYaw = camYaw + pi_half; (pi_half = PI/2)
  float vYaw = camYaw + (PI * 0.5f);
  float cY = std::cos(-vYaw);
  float sY = std::sin(-vYaw);

  float rx = x * cY - z * sY;
  float rz = x * sY + z * cY;
  x = rx;
  z = rz;

  // Pitch Rotation
  float cP = std::cos(-camPitch);
  float sP = std::sin(-camPitch);

  float ry = y * cP - z * sP;
  rz = y * sP + z * cP;
  y = ry;
  z = rz;

  return {x, y, z};
}

struct ray_t
{
  vec3 origin;
  vec3 direction;
};

// Ray-Plane Intersection
inline bool intersect_ray_plane(const vec3 &ray_origin, const vec3 &ray_dir,
                                const vec3 &plane_point,
                                const vec3 &plane_normal, float &t)
{
  float denom = dot(plane_normal, ray_dir);
  if (std::abs(denom) > 1e-6f)
  {
    vec3 p0l0 = plane_point - ray_origin;
    t = dot(p0l0, plane_normal) / denom;
    return (t >= 0);
  }
  return false;
}
// Ray-Sphere Intersection
inline bool intersect_ray_sphere(const vec3 &ray_origin, const vec3 &ray_dir,
                                 const vec3 &sphere_center, float sphere_radius,
                                 float &t)
{
  vec3 oc = ray_origin - sphere_center;
  float a = dot(ray_dir, ray_dir);
  float b = 2.0f * dot(oc, ray_dir);
  float c = dot(oc, oc) - sphere_radius * sphere_radius;
  float discriminant = b * b - 4 * a * c;

  if (discriminant < 0)
    return false;

  float sqrt_discriminant = std::sqrt(discriminant);
  float t1 = (-b - sqrt_discriminant) / (2.0f * a);
  float t2 = (-b + sqrt_discriminant) / (2.0f * a);

  // Return the closest intersection point
  t = (t1 >= 0) ? t1 : t2;
  return (t >= 0);
}


// Ray-AABB Intersection (Slab Method)
inline bool intersect_ray_aabb(const vec3 &ray_origin, const vec3 &ray_dir,
                               const vec3 &aabb_min, const vec3 &aabb_max,
                               float &t_min)
{
  float tx1 = (aabb_min.x - ray_origin.x) / ray_dir.x;
  float tx2 = (aabb_max.x - ray_origin.x) / ray_dir.x;

  float tmin = (tx1 < tx2) ? tx1 : tx2;
  float tmax = (tx1 > tx2) ? tx1 : tx2;

  float ty1 = (aabb_min.y - ray_origin.y) / ray_dir.y;
  float ty2 = (aabb_max.y - ray_origin.y) / ray_dir.y;

  tmin = (ty1 < ty2) ? std::max(tmin, ty1) : std::max(tmin, ty2);
  tmax = (ty1 > ty2) ? std::min(tmax, ty1) : std::min(tmax, ty2);

  float tz1 = (aabb_min.z - ray_origin.z) / ray_dir.z;
  float tz2 = (aabb_max.z - ray_origin.z) / ray_dir.z;

  tmin = (tz1 < tz2) ? std::max(tmin, tz1) : std::max(tmin, tz2);
  tmax = (tz1 > tz2) ? std::min(tmax, tz1) : std::min(tmax, tz2);

  if (tmax >= tmin && tmax >= 0.0f)
  {
    t_min = tmin;
    return true;
  }
  return false;
}

// Project View Space point to Screen Coordinates
inline vec2 view_to_screen(const vec3 &p, const vec2 &display_size, bool ortho,
                           float ortho_h, float fov_degrees)
{
  if (ortho)
  {
    float aspect_ratio = display_size.x / display_size.y;
    float h = ortho_h;
    float w = h * aspect_ratio;

    // Map p.x, p.y to [-1, 1] based on ortho rect
    float x_ndc = p.x / (w * 0.5f);
    float y_ndc = p.y / (h * 0.5f);

    return {(x_ndc * 0.5f + 0.5f) * display_size.x,
            (1.0f - (y_ndc * 0.5f + 0.5f)) * display_size.y};
  }
  else
  {
    float aspect_ratio = display_size.x / display_size.y;
    float tan_half = std::tan(to_radians(fov_degrees) * 0.5f);

    // Looking down -Z.
    float x_ndc = p.x / (-p.z * tan_half * aspect_ratio);
    float y_ndc = p.y / (-p.z * tan_half);

    return {(x_ndc * 0.5f + 0.5f) * display_size.x,
            (1.0f - (y_ndc * 0.5f + 0.5f)) * display_size.y};
  }
}

// AABB Intersection
inline bool intersect_aabb_aabb(const vec3 &min_a, const vec3 &max_a,
                                const vec3 &min_b, const vec3 &max_b)
{
  return (min_a.x <= max_b.x && max_a.x >= min_b.x) &&
         (min_a.y <= max_b.y && max_a.y >= min_b.y) &&
         (min_a.z <= max_b.z && max_a.z >= min_b.z);
}

inline bool intersect_AABB_AABB_from_center_and_half_extents(
    const vec3 &center_a, const vec3 &half_extents_a, const vec3 &center_b,
    const vec3 &half_extents_b)
{
  vec3 min_a = center_a - half_extents_a;
  vec3 max_a = center_a + half_extents_a;
  vec3 min_b = center_b - half_extents_b;
  vec3 max_b = center_b + half_extents_b;
  return intersect_aabb_aabb(min_a, max_a, min_b, max_b);
}

// Line Clipping against Near Z Plane (default -0.1f)
inline bool clip_line(vec3 &p1, vec3 &p2, float near_z = -0.1f)
{
  if (p1.z > near_z && p2.z > near_z)
    return false;

  if (p1.z > near_z)
  {
    float t = (near_z - p1.z) / (p2.z - p1.z);
    p1 = mix(p1, p2, t);
    p1.z = near_z; // ensure precision
  }
  else if (p2.z > near_z)
  {
    float t = (near_z - p2.z) / (p1.z - p2.z);
    p2 = mix(p2, p1, t);
    p2.z = near_z;
  }
  return true;
}

} // namespace linalg
