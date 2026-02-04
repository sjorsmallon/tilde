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

template <typename Type> inline Type degrees_to_radians(Type degrees)
{
  return degrees * (Type(3.14159265359) / Type(180.0));
}

template <typename Type> inline Type radians_to_degrees(Type radians)
{
  return radians * (Type(180.0) / Type(3.14159265359));
}

} // namespace shared