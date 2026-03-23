#include "components.hpp"
#include "../asset.hpp"
#include "../linalg.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace network
{

// Schema registration for material_t
DEFINE_SCHEMA_CLASS(material_t)
{
  BEGIN_SCHEMA_FIELDS()
    REGISTER_SCHEMA_FIELD(shader_type);
    REGISTER_SCHEMA_FIELD(color);
    REGISTER_SCHEMA_FIELD(roughness);
  END_SCHEMA_FIELDS()
}

// Schema registration for render_component_t
DEFINE_SCHEMA_CLASS(render_component_t)
{
  BEGIN_SCHEMA_FIELDS()
    REGISTER_SCHEMA_FIELD(mesh_id);
    REGISTER_SCHEMA_FIELD(mesh_path);
    REGISTER_SCHEMA_FIELD(visible);
    REGISTER_SCHEMA_FIELD(is_wireframe);
    REGISTER_SCHEMA_FIELD(offset);
    REGISTER_SCHEMA_FIELD(scale);
    REGISTER_SCHEMA_FIELD(rotation);
    REGISTER_SCHEMA_FIELD(material);
  END_SCHEMA_FIELDS()
}

// Schema registration for hitbox_component_t
DEFINE_SCHEMA_CLASS(hitbox_component_t)
{
  BEGIN_SCHEMA_FIELDS()
    REGISTER_SCHEMA_FIELD(shape_type);
    REGISTER_SCHEMA_FIELD(size);
    REGISTER_SCHEMA_FIELD(offset);
  END_SCHEMA_FIELDS()
}

void set_primitive_render(render_component_t& rc, const char* primitive_name, vec3f size)
{
  // Ensure primitive mesh is loaded/cached
  assets::get_primitive_mesh(primitive_name);

  // Set up the render component
  std::string path = std::string("__primitive_") + primitive_name;
  rc.mesh_path.set(path.c_str());
  rc.mesh_id = -1; // Not using ID-based lookup
  rc.scale = size;
  rc.visible = true;
}

// --- Hitbox collision implementations ---

bool test_sphere_sphere(const vec3f& sphere_a_pos, float radius_a,
                        const vec3f& sphere_b_pos, float radius_b)
{
  vec3f delta = sphere_b_pos - sphere_a_pos;
  float dist_squared = linalg::dot(delta, delta);
  float radius_sum = radius_a + radius_b;
  return dist_squared <= (radius_sum * radius_sum);
}

bool test_sphere_capsule(const vec3f& sphere_pos, float sphere_radius,
                         const vec3f& capsule_p0, const vec3f& capsule_p1,
                         float capsule_radius)
{
  // Find closest point on line segment (capsule axis) to sphere center
  vec3f segment = capsule_p1 - capsule_p0;
  vec3f to_sphere = sphere_pos - capsule_p0;

  float segment_length_sq = linalg::dot(segment, segment);

  // Handle degenerate case (capsule is a sphere)
  if (segment_length_sq < 0.0001f)
  {
    return test_sphere_sphere(sphere_pos, sphere_radius, capsule_p0, capsule_radius);
  }

  // Project sphere center onto line segment
  float t = linalg::dot(to_sphere, segment) / segment_length_sq;
  t = std::clamp(t, 0.0f, 1.0f);

  vec3f closest_point = capsule_p0 + segment * t;

  // Test sphere vs sphere at closest point
  return test_sphere_sphere(sphere_pos, sphere_radius, closest_point, capsule_radius);
}

bool test_sphere_aabb(const vec3f& sphere_pos, float sphere_radius,
                      const vec3f& aabb_min, const vec3f& aabb_max)
{
  // Find closest point on AABB to sphere center
  vec3f closest = {
    std::clamp(sphere_pos.x, aabb_min.x, aabb_max.x),
    std::clamp(sphere_pos.y, aabb_min.y, aabb_max.y),
    std::clamp(sphere_pos.z, aabb_min.z, aabb_max.z)
  };

  vec3f delta = sphere_pos - closest;
  float dist_squared = linalg::dot(delta, delta);
  return dist_squared <= (sphere_radius * sphere_radius);
}

bool test_hitbox_collision(const vec3f& entity_a_pos, const hitbox_component_t& hitbox_a,
                           const vec3f& entity_b_pos, const hitbox_component_t& hitbox_b)
{
  // Apply offsets to get world-space hitbox centers
  vec3f pos_a = entity_a_pos + hitbox_a.offset;
  vec3f pos_b = entity_b_pos + hitbox_b.offset;

  // Get shape type strings
  const char* shape_a = hitbox_a.shape_type.c_str();
  const char* shape_b = hitbox_b.shape_type.c_str();

  // Sphere vs Sphere
  if (strcmp(shape_a, "sphere") == 0 && strcmp(shape_b, "sphere") == 0)
  {
    return test_sphere_sphere(pos_a, hitbox_a.size.x, pos_b, hitbox_b.size.x);
  }

  // Sphere vs Capsule
  if (strcmp(shape_a, "sphere") == 0 && strcmp(shape_b, "capsule") == 0)
  {
    float half_height = hitbox_b.size.y;
    vec3f capsule_p0 = {pos_b.x, pos_b.y - half_height, pos_b.z};
    vec3f capsule_p1 = {pos_b.x, pos_b.y + half_height, pos_b.z};
    return test_sphere_capsule(pos_a, hitbox_a.size.x, capsule_p0, capsule_p1, hitbox_b.size.x);
  }

  // Capsule vs Sphere (reverse)
  if (strcmp(shape_a, "capsule") == 0 && strcmp(shape_b, "sphere") == 0)
  {
    float half_height = hitbox_a.size.y;
    vec3f capsule_p0 = {pos_a.x, pos_a.y - half_height, pos_a.z};
    vec3f capsule_p1 = {pos_a.x, pos_a.y + half_height, pos_a.z};
    return test_sphere_capsule(pos_b, hitbox_b.size.x, capsule_p0, capsule_p1, hitbox_a.size.x);
  }

  // Sphere vs AABB
  if (strcmp(shape_a, "sphere") == 0 && strcmp(shape_b, "aabb") == 0)
  {
    vec3f aabb_min = pos_b - hitbox_b.size;
    vec3f aabb_max = pos_b + hitbox_b.size;
    return test_sphere_aabb(pos_a, hitbox_a.size.x, aabb_min, aabb_max);
  }

  // AABB vs Sphere (reverse)
  if (strcmp(shape_a, "aabb") == 0 && strcmp(shape_b, "sphere") == 0)
  {
    vec3f aabb_min = pos_a - hitbox_a.size;
    vec3f aabb_max = pos_a + hitbox_a.size;
    return test_sphere_aabb(pos_b, hitbox_b.size.x, aabb_min, aabb_max);
  }

  // AABB vs AABB
  if (strcmp(shape_a, "aabb") == 0 && strcmp(shape_b, "aabb") == 0)
  {
    vec3f a_min = pos_a - hitbox_a.size;
    vec3f a_max = pos_a + hitbox_a.size;
    vec3f b_min = pos_b - hitbox_b.size;
    vec3f b_max = pos_b + hitbox_b.size;

    return (a_min.x <= b_max.x && a_max.x >= b_min.x) &&
           (a_min.y <= b_max.y && a_max.y >= b_min.y) &&
           (a_min.z <= b_max.z && a_max.z >= b_min.z);
  }

  // Capsule vs Capsule - simplified as AABB for now
  // (proper capsule-capsule is complex, would need GJK or specialized algorithm)
  if (strcmp(shape_a, "capsule") == 0 && strcmp(shape_b, "capsule") == 0)
  {
    // Approximate as cylinder AABBs
    float r_a = hitbox_a.size.x;
    float h_a = hitbox_a.size.y * 2.0f + r_a * 2.0f; // total height including caps
    vec3f a_min = {pos_a.x - r_a, pos_a.y - h_a * 0.5f, pos_a.z - r_a};
    vec3f a_max = {pos_a.x + r_a, pos_a.y + h_a * 0.5f, pos_a.z + r_a};

    float r_b = hitbox_b.size.x;
    float h_b = hitbox_b.size.y * 2.0f + r_b * 2.0f;
    vec3f b_min = {pos_b.x - r_b, pos_b.y - h_b * 0.5f, pos_b.z - r_b};
    vec3f b_max = {pos_b.x + r_b, pos_b.y + h_b * 0.5f, pos_b.z + r_b};

    return (a_min.x <= b_max.x && a_max.x >= b_min.x) &&
           (a_min.y <= b_max.y && a_max.y >= b_min.y) &&
           (a_min.z <= b_max.z && a_max.z >= b_min.z);
  }

  // Unknown shape combination - no collision by default
  return false;
}

} // namespace network
