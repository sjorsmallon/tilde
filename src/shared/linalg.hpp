#pragma once

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

inline float length(const vec3 &v) { return std::sqrt(dot(v, v)); }

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

// Math Helpers
constexpr float PI = 3.14159265359f;

inline float to_radians(float degrees) { return degrees * (PI / 180.0f); }

inline float to_degrees(float radians) { return radians * (180.0f / PI); }

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
    float aspect = display_size.x / display_size.y;
    float h = ortho_h;
    float w = h * aspect;

    // Map p.x, p.y to [-1, 1] based on ortho rect
    float x_ndc = p.x / (w * 0.5f);
    float y_ndc = p.y / (h * 0.5f);

    return {(x_ndc * 0.5f + 0.5f) * display_size.x,
            (1.0f - (y_ndc * 0.5f + 0.5f)) * display_size.y};
  }
  else
  {
    float aspect = display_size.x / display_size.y;
    float tanHalf = std::tan(to_radians(fov_degrees) * 0.5f);

    // Looking down -Z.
    float x_ndc = p.x / (-p.z * tanHalf * aspect);
    float y_ndc = p.y / (-p.z * tanHalf);

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
