#include "key_names.hpp"

#include <cctype>

namespace client::input
{

namespace
{

struct key_name_row_t
{
  key_t            key;
  std::string_view name;
};

constexpr Enum_Array<key_t, key_name_row_t> KEY_NAMES = {{
    {key_t::Unknown, "unknown"},

    {key_t::A, "a"}, {key_t::B, "b"}, {key_t::C, "c"}, {key_t::D, "d"}, {key_t::E, "e"},
    {key_t::F, "f"}, {key_t::G, "g"}, {key_t::H, "h"}, {key_t::I, "i"}, {key_t::J, "j"},
    {key_t::K, "k"}, {key_t::L, "l"}, {key_t::M, "m"}, {key_t::N, "n"}, {key_t::O, "o"},
    {key_t::P, "p"}, {key_t::Q, "q"}, {key_t::R, "r"}, {key_t::S, "s"}, {key_t::T, "t"},
    {key_t::U, "u"}, {key_t::V, "v"}, {key_t::W, "w"}, {key_t::X, "x"}, {key_t::Y, "y"},
    {key_t::Z, "z"},

    {key_t::Num_0, "0"}, {key_t::Num_1, "1"}, {key_t::Num_2, "2"}, {key_t::Num_3, "3"},
    {key_t::Num_4, "4"}, {key_t::Num_5, "5"}, {key_t::Num_6, "6"}, {key_t::Num_7, "7"},
    {key_t::Num_8, "8"}, {key_t::Num_9, "9"},

    {key_t::F1, "f1"}, {key_t::F2, "f2"}, {key_t::F3, "f3"}, {key_t::F4, "f4"},
    {key_t::F5, "f5"}, {key_t::F6, "f6"}, {key_t::F7, "f7"}, {key_t::F8, "f8"},
    {key_t::F9, "f9"}, {key_t::F10, "f10"}, {key_t::F11, "f11"}, {key_t::F12, "f12"},

    {key_t::Space, "space"}, {key_t::Tab, "tab"}, {key_t::Enter, "enter"},
    {key_t::Backspace, "backspace"}, {key_t::Delete, "delete"}, {key_t::Escape, "escape"},

    {key_t::Left_Shift, "lshift"}, {key_t::Right_Shift, "rshift"},
    {key_t::Left_Ctrl, "lctrl"},   {key_t::Right_Ctrl, "rctrl"},
    {key_t::Left_Alt, "lalt"},     {key_t::Right_Alt, "ralt"},
    {key_t::Left_Gui, "lgui"},     {key_t::Right_Gui, "rgui"},

    {key_t::Arrow_Left, "leftarrow"}, {key_t::Arrow_Right, "rightarrow"},
    {key_t::Arrow_Up, "uparrow"},     {key_t::Arrow_Down, "downarrow"},
    {key_t::Page_Up, "pgup"},         {key_t::Page_Down, "pgdn"},
    {key_t::End, "end"},

    {key_t::Left_Bracket, "["}, {key_t::Right_Bracket, "]"}, {key_t::Tilde, "`"},

    {key_t::Keypad_0, "kp_0"}, {key_t::Keypad_1, "kp_1"}, {key_t::Keypad_2, "kp_2"},
    {key_t::Keypad_3, "kp_3"}, {key_t::Keypad_4, "kp_4"}, {key_t::Keypad_5, "kp_5"},
    {key_t::Keypad_6, "kp_6"}, {key_t::Keypad_7, "kp_7"}, {key_t::Keypad_8, "kp_8"},
    {key_t::Keypad_9, "kp_9"},
}};

static_assert(rows_in_enum_order<&key_name_row_t::key>(KEY_NAMES),
              "KEY_NAMES must have one row per key_t, in enum order");

bool equals_ignoring_case(std::string_view left, std::string_view right)
{
  if (left.size() != right.size())
    return false;
  for (size_t index = 0; index < left.size(); ++index)
    if (std::tolower((unsigned char)left[index]) != std::tolower((unsigned char)right[index]))
      return false;
  return true;
}

} // namespace

std::string_view key_name(key_t key)
{
  return KEY_NAMES[key].name;
}

std::optional<key_t> try_key_from_name(std::string_view name)
{
  for (const key_name_row_t& row : KEY_NAMES)
    if (row.key != key_t::Unknown && equals_ignoring_case(row.name, name))
      return row.key;
  return std::nullopt;
}

} // namespace client::input
