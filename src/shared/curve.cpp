#include "curve.hpp"

#include "log.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

float centripetal_knot_step(const linalg::vec3 &from, const linalg::vec3 &to)
{
  return std::max(std::sqrt(linalg::length(to - from)), 1e-4f);
}

linalg::vec3 mix(const linalg::vec3 &a, const linalg::vec3 &b, float from, float to, float at)
{
  const float weight = (at - from) / (to - from);
  return a * (1.f - weight) + b * weight;
}

// Barry-Goldman evaluation of the segment p1 to p2 at knot `t` in [t1, t2].
linalg::vec3 evaluate_segment(const linalg::vec3 &p0, const linalg::vec3 &p1,
                              const linalg::vec3 &p2, const linalg::vec3 &p3, float t)
{
  const float t0 = 0.f;
  const float t1 = t0 + centripetal_knot_step(p0, p1);
  const float t2 = t1 + centripetal_knot_step(p1, p2);
  const float t3 = t2 + centripetal_knot_step(p2, p3);
  const float at = t1 + (t2 - t1) * t;

  const linalg::vec3 a1 = mix(p0, p1, t0, t1, at);
  const linalg::vec3 a2 = mix(p1, p2, t1, t2, at);
  const linalg::vec3 a3 = mix(p2, p3, t2, t3, at);
  const linalg::vec3 b1 = mix(a1, a2, t0, t2, at);
  const linalg::vec3 b2 = mix(a2, a3, t1, t3, at);
  return mix(b1, b2, t1, t2, at);
}

} // namespace

std::vector<linalg::vec3> sample_catmull_rom(Span<const linalg::vec3> control_points,
                                             int samples_per_segment)
{
  if (control_points.size() < 2)
    fatal_error("sample_catmull_rom: {} control points, at least two needed",
                control_points.size());
  if (samples_per_segment < 1)
    fatal_error("sample_catmull_rom: {} samples per segment, at least one needed",
                samples_per_segment);

  const size_t count = control_points.size();
  const auto point_at = [&control_points, count](long index) -> linalg::vec3 {
    if (index < 0)
      return control_points[0] * 2.f - control_points[1];
    if (index >= (long)count)
      return control_points[count - 1] * 2.f - control_points[count - 2];
    return control_points[(size_t)index];
  };

  std::vector<linalg::vec3> samples;
  samples.reserve((count - 1) * (size_t)samples_per_segment + 1);
  for (long segment = 0; segment + 1 < (long)count; ++segment)
  {
    const linalg::vec3 p0 = point_at(segment - 1);
    const linalg::vec3 p1 = point_at(segment);
    const linalg::vec3 p2 = point_at(segment + 1);
    const linalg::vec3 p3 = point_at(segment + 2);
    for (int sample = 0; sample < samples_per_segment; ++sample)
    {
      if (sample == 0)
      {
        samples.push_back(p1);
        continue;
      }
      samples.push_back(evaluate_segment(p0, p1, p2, p3,
                                         (float)sample / (float)samples_per_segment));
    }
  }
  samples.push_back(control_points[count - 1]);
  return samples;
}

} // namespace shared
