#pragma once
// #include <cmath>
// #include <cstdint>

// template <float Min, float Max, float Precision> struct QuantizedFloat
// {
//   static constexpr float Range = Max - Min;
//   static constexpr int Steps = int(Range / Precision) + 1;
//   static constexpr int Bits = ceil_log2(Steps);

//   static uint32_t encode(float v)
//   {
//     return uint32_t((v - Min) / Precision + 0.5f);
//   }

//   static float decode(uint32_t q) { return Min + q * Precision; }
// };

// zero / integer-only / integer + 5-bit fraction