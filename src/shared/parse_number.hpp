#pragma once

#include <charconv>
#include <system_error>
#include <type_traits>
#include <version>

// THE ONE PLACE TEXT BECOMES A NUMBER, because half the standard's answer is
// missing on one of the toolchains this builds with and no call site should
// have to know which.
//
// std::from_chars is the right primitive and this is not a replacement for it:
// it does not allocate, never consults the locale, needs no NUL terminator, and
// hands back a `ptr` so a scan can continue -- which is exactly what
// parse_float_components and every generated try_parse_whole do. libc++ ships
// the INTEGER overloads and declares the FLOATING-POINT ones DELETED (Apple
// clang 16, which is the macOS compiler), so `std::from_chars(begin, end,
// some_float)` is a compile error there and compiles everywhere else. That
// asymmetry is the whole reason this header exists; the integer path below is a
// straight forward, so there is one spelling at the call sites rather than two
// that differ by which type happens to be broken.
//
// The return type is the standard's own, deliberately. A caller mid-scan reads
// `.ptr` to find where the next component starts, so an optional<T> would throw
// away the half of the answer those callers need -- which is why this carries
// the try_ prefix (it is fallible) without carrying an optional.

#if !defined(__cpp_lib_to_chars)

  #include <cerrno>
  #include <cmath>
  #include <cstdlib>
  #include <cstring>
  #include <locale.h>
  #include <string>

  #if defined(__APPLE__)
    #include <xlocale.h>
  #endif

namespace parse_number_detail
{

// The C locale, once. strtod's decimal point is LOCALE-dependent, so under a
// locale that separates with ',' it reads "1.5" as 1 -- a map file that parses
// differently depending on who is running it. from_chars has no locale, and a
// shim for it must not acquire one.
inline locale_t c_locale()
{
  static locale_t locale = newlocale(LC_ALL_MASK, "C", (locale_t)0);
  return locale;
}

// The longest prefix of [begin, end) matching from_chars' general grammar:
//
//   '-'? ( digit+ ( '.' digit* )? | '.' digit+ ) ( [eE] [+-]? digit+ )?
//
// Scanning it here is what keeps the shim from being MORE permissive than the
// real thing: strtod also takes a leading '+', leading whitespace, "0x" hex and
// "inf"/"nan", and every one of those is a map file that loads on macOS and is
// refused on Windows. It is also what makes the copy below safe -- the caller's
// range can be a view into the middle of a console line with no NUL anywhere,
// and strtod reads on until it stops matching.
inline const char* scan_decimal(const char* begin, const char* end)
{
  const auto is_digit = [](char character) { return character >= '0' && character <= '9'; };

  const char* cursor = begin;
  if (cursor != end && *cursor == '-')
    ++cursor;

  int32_t digit_count = 0;
  while (cursor != end && is_digit(*cursor))
  {
    ++cursor;
    ++digit_count;
  }
  if (cursor != end && *cursor == '.')
  {
    ++cursor;
    while (cursor != end && is_digit(*cursor))
    {
      ++cursor;
      ++digit_count;
    }
  }

  // A '.' with no digit on either side is not a number, and neither is a lone
  // '-'. Reporting no match lets the caller say invalid_argument, as from_chars
  // does, rather than handing back a zero the text never said.
  if (digit_count == 0)
    return begin;

  // An exponent counts only when a digit follows it: "1e" is the number 1 with
  // a trailing 'e', which is how from_chars reports it too.
  const char* exponent = cursor;
  if (exponent != end && (*exponent == 'e' || *exponent == 'E'))
  {
    ++exponent;
    if (exponent != end && (*exponent == '+' || *exponent == '-'))
      ++exponent;
    if (exponent != end && is_digit(*exponent))
    {
      while (exponent != end && is_digit(*exponent))
        ++exponent;
      cursor = exponent;
    }
  }

  return cursor;
}

template <typename Float_T>
std::from_chars_result parse_float(const char* begin, const char* end, Float_T& value)
{
  const char* stop = scan_decimal(begin, end);
  if (stop == begin)
    return {begin, std::errc::invalid_argument};

  // strtod needs a NUL terminator. The scan already bounded the token, so the
  // copy is the match and nothing past it; 64 bytes holds anything %.9g or
  // %.17g can write, and the heap arm is for text no writer of ours produces.
  const size_t length = (size_t)(stop - begin);
  char         stack_buffer[64];
  std::string  heap_buffer;
  const char*  text = nullptr;
  if (length < sizeof(stack_buffer))
  {
    std::memcpy(stack_buffer, begin, length);
    stack_buffer[length] = '\0';
    text                 = stack_buffer;
  }
  else
  {
    heap_buffer.assign(begin, length);
    text = heap_buffer.c_str();
  }

  const locale_t locale     = c_locale();
  char*          parse_end  = nullptr;
  errno                     = 0;

  Float_T parsed = Float_T(0);
  if constexpr (std::is_same_v<Float_T, float>)
    parsed = locale != (locale_t)0 ? strtof_l(text, &parse_end, locale) : strtof(text, &parse_end);
  else
    parsed = locale != (locale_t)0 ? strtod_l(text, &parse_end, locale) : strtod(text, &parse_end);

  // strtod reports ERANGE for overflow AND for gradual underflow to a
  // subnormal; from_chars only refuses what the type cannot represent, and a
  // subnormal can be. So the refusal is keyed on the VALUE, not on errno alone,
  // and `value` is left untouched exactly as from_chars leaves it.
  if (errno == ERANGE && (parsed == Float_T(0) || std::isinf(parsed)))
    return {stop, std::errc::result_out_of_range};

  value = parsed;
  return {stop, std::errc{}};
}

} // namespace parse_number_detail

#endif // !__cpp_lib_to_chars

// Drop-in for std::from_chars over every numeric type the field tables carry.
// Global scope, beside the other house primitives (Span, Array, enum_traits),
// because "text to number" belongs to no one family -- the reflection layer,
// the run-time file reader and the generated cvar and command parsers all ask
// it.
template <typename Number_T>
[[nodiscard]] std::from_chars_result try_parse_number(const char* begin, const char* end,
                                                      Number_T& value)
{
  static_assert(std::is_arithmetic_v<Number_T> && !std::is_same_v<Number_T, bool>,
                "try_parse_number reads numbers; a bool has its own closed spelling");

  if constexpr (std::is_floating_point_v<Number_T>)
  {
#if defined(__cpp_lib_to_chars)
    return std::from_chars(begin, end, value);
#else
    return parse_number_detail::parse_float(begin, end, value);
#endif
  }
  else
  {
    return std::from_chars(begin, end, value);
  }
}
