#pragma once

namespace shared
{

template <typename Type>
inline Type clamp(Type value, const Type min, const Type max)
{
  return (value < min) ? min : (value > max) ? max : value;
}

template <typename Type>
inline void clamp_this(Type &value, const Type min, const Type max)
{
  value = clamp(value, min, max);
}

// Not clamped to [0,1] -- extrapolates if you hand it a t outside that. Keep it
// for the cases where that is the intent (a user-tunable blend that may sensibly
// over-shoot); reach for lerp_clamped anywhere the result is meant to stay
// BETWEEN the endpoints.
template <typename Type> inline Type lerp(Type from, Type to, Type t)
{
  return from + (to - from) * t;
}

// Interpolation that cannot become extrapolation, because t is pinned first.
//
// The distinction is load-bearing rather than cosmetic. Interpolating known
// samples is bracketed by truth: it can only be LATE. Projecting past the newest
// one is a guess, and it is wrong exactly when the subject changes behaviour --
// which for a remote player means limbs sliding through walls under jitter, with
// no error and no log to say so. Every snapshot blend on both sides of the wire
// goes through here so that property is structural instead of remembered at each
// call site.
template <typename Type> inline Type lerp_clamped(Type from, Type to, Type t)
{
  return from + (to - from) * clamp(t, Type(0), Type(1));
}

template <typename Type> inline Type degrees_to_radians(Type degrees)
{
  return degrees * (Type(3.14159265359) / Type(180.0));
}

template <typename Type> inline Type radians_to_degrees(Type radians)
{
  return radians * (Type(180.0) / Type(3.14159265359));
}

} // namespace shared