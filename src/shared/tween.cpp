#include "tween.hpp"

#include <cmath>

namespace shared
{

float apply_easing(entities::Easing easing, float t)
{
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;

  const float inverted = 1.0f - t;

  switch (easing)
  {
    case entities::Easing::Linear: return t;
    case entities::Easing::Smooth: return t * t * (3.0f - 2.0f * t);
    case entities::Easing::In_Cubic: return t * t * t;
    case entities::Easing::Out_Cubic: return 1.0f - inverted * inverted * inverted;
    case entities::Easing::In_Out_Cubic:
      return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * inverted * inverted * inverted;
  }
  return t;
}

linalg::quatf slerp(const linalg::quatf& from, const linalg::quatf& to, float t)
{
  float         cosine = linalg::dot(from, to);
  linalg::quatf target = to;
  if (cosine < 0.0f)
  {
    cosine = -cosine;
    target = {-to.x, -to.y, -to.z, -to.w};
  }
  if (cosine > 0.9995f)
    return linalg::nlerp(from, target, t);

  const float angle       = std::acos(cosine);
  const float sine        = std::sin(angle);
  const float from_weight = std::sin((1.0f - t) * angle) / sine;
  const float to_weight   = std::sin(t * angle) / sine;
  return linalg::normalize(linalg::quatf{from.x * from_weight + target.x * to_weight,
                                         from.y * from_weight + target.y * to_weight,
                                         from.z * from_weight + target.z * to_weight,
                                         from.w * from_weight + target.w * to_weight});
}

float evaluate(const tween_t& tween, float age)
{
  const float linear_t = tween.duration <= 0.0f ? 1.0f : age / tween.duration;
  return tween.from + (tween.to - tween.from) * apply_easing(tween.easing, linear_t);
}

float ping_pong(float age, float period)
{
  if (period <= 0.0f)
    return 0.0f;
  const float wrapped = std::fmod(std::fabs(age), 2.0f * period);
  return wrapped <= period ? wrapped : 2.0f * period - wrapped;
}

} // namespace shared
