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

// Not clamped to [0,1] -- extrapolates if you hand it a t outside that.
template <typename Type> inline Type lerp(Type from, Type to, Type t)
{
  return from + (to - from) * t;
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