#pragma once

// ONE evaluator for "two values, a duration, an easing". A tween is a VALUE on
// the record that owns the animated thing, evaluated from that record's own
// stamp (a tick the server wrote once, or a birth time on the frame clock);
// nothing advances an offset per frame. The mover's path segment and the UI's
// node animations both go through here, so there is one Easing and one curve.

#include "entities/generated/entities_generated.hpp"
#include "linalg.hpp"

namespace shared
{

// Clamped: cubic curves are not bounded outside [0, 1].
[[nodiscard]] float apply_easing(entities::Easing easing, float t);

[[nodiscard]] linalg::quatf slerp(const linalg::quatf& from, const linalg::quatf& to, float t);

struct tween_t
{
  float            from     = 0.0f;
  float            to       = 1.0f;
  float            duration = 1.0f;
  entities::Easing easing   = entities::Easing::Linear;
};

// Holds `to` past the duration, and `from` before zero.
[[nodiscard]] float evaluate(const tween_t& tween, float age);

// Folds an age onto [0, period] going up then down, so any tween loops as a bob.
[[nodiscard]] float ping_pong(float age, float period);

} // namespace shared
