#pragma once

#include <cmath>
#include <cstdint>

namespace linalg {

template <typename T> struct vec2_t {
  union {
    struct {
      T x, y;
    };
    struct {
      T u, v;
    };
    T data[2];
  };

  T &operator[](int i) { return data[i]; }
  const T &operator[](int i) const { return data[i]; }
};

template <typename T> struct vec3_t {
  union {
    struct {
      T x, y, z;
    };
    struct {
      T r, g, b;
    };
    T data[3];
  };

  T &operator[](int i) { return data[i]; }
  const T &operator[](int i) const { return data[i]; }
};

template <typename T> struct vec4_t {
  union {
    struct {
      T x, y, z, w;
    };
    struct {
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
struct mat3f {
  vec3 cols[3];

  vec3 &operator[](int i) { return cols[i]; }
  const vec3 &operator[](int i) const { return cols[i]; }

  static mat3f identity() { return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; }
  static mat3f diag(float v) { return {{{v, 0, 0}, {0, v, 0}, {0, 0, v}}}; }
  static mat3f from_cols(const vec3 &c0, const vec3 &c1, const vec3 &c2) {
    return {{c0, c1, c2}};
  }
};

// Matrix 4x4 (Column Major)
struct mat4f {
  vec4 cols[4];

  vec4 &operator[](int i) { return cols[i]; }
  const vec4 &operator[](int i) const { return cols[i]; }

  static mat4f identity() {
    return {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  }
  static mat4f diag(float v) {
    return {{{v, 0, 0, 0}, {0, v, 0, 0}, {0, 0, v, 0}, {0, 0, 0, v}}};
  }
  static mat4f from_cols(const vec4 &c0, const vec4 &c1, const vec4 &c2,
                         const vec4 &c3) {
    return {{c0, c1, c2, c3}};
  }
};

// --- Free Functions ---

// Vec3 Operations
template <typename T>
inline vec3_t<T> operator+(const vec3_t<T> &a, const vec3_t<T> &b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

template <typename T>
inline vec3_t<T> operator-(const vec3_t<T> &a, const vec3_t<T> &b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

template <typename T> inline vec3_t<T> operator*(const vec3_t<T> &a, T s) {
  return {a.x * s, a.y * s, a.z * s};
}

template <typename T> inline vec3_t<T> operator*(T s, const vec3_t<T> &a) {
  return a * s;
}

template <typename T> inline T dot(const vec3_t<T> &a, const vec3_t<T> &b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

template <typename T>
inline vec3_t<T> cross(const vec3_t<T> &a, const vec3_t<T> &b) {
  return {
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x,
  };
}

inline float length(const vec3 &v) { return std::sqrt(dot(v, v)); }

inline vec3 normalize(const vec3 &v) {
  float l = length(v);
  if (l > 1e-6f)
    return v * (1.0f / l);
  return {0, 0, 0};
}

// Vec4 Operations
template <typename T>
inline vec4_t<T> operator+(const vec4_t<T> &a, const vec4_t<T> &b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

template <typename T> inline vec4_t<T> operator*(const vec4_t<T> &a, T s) {
  return {a.x * s, a.y * s, a.z * s, a.w * s};
}

// Mat4 Operations
inline vec4 operator*(const mat4f &m, const vec4 &v) {
  return m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w;
}

inline mat4f operator*(const mat4f &a, const mat4f &b) {
  mat4f res = {}; // init zero
  for (int i = 0; i < 4; ++i) {
    res.cols[i] = a * b.cols[i];
  }
  return res;
}

// Math Helpers
constexpr float PI = 3.14159265359f;

inline float to_radians(float degrees) { return degrees * (PI / 180.0f); }

inline float to_degrees(float radians) { return radians * (180.0f / PI); }

template <typename T> inline T mix(T a, T b, float t) {
  return a * (1.0f - t) + b * t;
}

} // namespace linalg
