#include "input.hpp"

#include "imgui.h"
#include <SDL.h>
#include "../shared/log.hpp"
#include "raw_input_win32.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iterator>
#include <vector>

namespace client::input
{

namespace
{

// --- key_t <-> SDL scancode tables -------------------------------------------
//
// One bidirectional source of truth: each row pairs a key_t with its SDL
// scancode. Both lookup tables (key_t -> scancode, scancode -> key_t) are built
// from this list at compile time, so they can never drift.

constexpr size_t key_count = static_cast<size_t>(key_t::Count);
constexpr size_t mouse_button_count = static_cast<size_t>(mouse_button_t::Count);

struct key_mapping_t
{
  key_t key;
  int scancode;
};

constexpr key_mapping_t key_mappings[] = {
    // Letters
    {key_t::A, SDL_SCANCODE_A}, {key_t::B, SDL_SCANCODE_B},
    {key_t::C, SDL_SCANCODE_C}, {key_t::D, SDL_SCANCODE_D},
    {key_t::E, SDL_SCANCODE_E}, {key_t::F, SDL_SCANCODE_F},
    {key_t::G, SDL_SCANCODE_G}, {key_t::H, SDL_SCANCODE_H},
    {key_t::I, SDL_SCANCODE_I}, {key_t::J, SDL_SCANCODE_J},
    {key_t::K, SDL_SCANCODE_K}, {key_t::L, SDL_SCANCODE_L},
    {key_t::M, SDL_SCANCODE_M}, {key_t::N, SDL_SCANCODE_N},
    {key_t::O, SDL_SCANCODE_O}, {key_t::P, SDL_SCANCODE_P},
    {key_t::Q, SDL_SCANCODE_Q}, {key_t::R, SDL_SCANCODE_R},
    {key_t::S, SDL_SCANCODE_S}, {key_t::T, SDL_SCANCODE_T},
    {key_t::U, SDL_SCANCODE_U}, {key_t::V, SDL_SCANCODE_V},
    {key_t::W, SDL_SCANCODE_W}, {key_t::X, SDL_SCANCODE_X},
    {key_t::Y, SDL_SCANCODE_Y}, {key_t::Z, SDL_SCANCODE_Z},

    // Digits (top row)
    {key_t::Num_0, SDL_SCANCODE_0}, {key_t::Num_1, SDL_SCANCODE_1},
    {key_t::Num_2, SDL_SCANCODE_2}, {key_t::Num_3, SDL_SCANCODE_3},
    {key_t::Num_4, SDL_SCANCODE_4}, {key_t::Num_5, SDL_SCANCODE_5},
    {key_t::Num_6, SDL_SCANCODE_6}, {key_t::Num_7, SDL_SCANCODE_7},
    {key_t::Num_8, SDL_SCANCODE_8}, {key_t::Num_9, SDL_SCANCODE_9},

    // Function keys
    {key_t::F1, SDL_SCANCODE_F1}, {key_t::F2, SDL_SCANCODE_F2},
    {key_t::F3, SDL_SCANCODE_F3}, {key_t::F4, SDL_SCANCODE_F4},
    {key_t::F5, SDL_SCANCODE_F5}, {key_t::F6, SDL_SCANCODE_F6},
    {key_t::F7, SDL_SCANCODE_F7}, {key_t::F8, SDL_SCANCODE_F8},
    {key_t::F9, SDL_SCANCODE_F9}, {key_t::F10, SDL_SCANCODE_F10},
    {key_t::F11, SDL_SCANCODE_F11}, {key_t::F12, SDL_SCANCODE_F12},

    // Whitespace / control
    {key_t::Space, SDL_SCANCODE_SPACE},
    {key_t::Tab, SDL_SCANCODE_TAB},
    {key_t::Enter, SDL_SCANCODE_RETURN},
    {key_t::Backspace, SDL_SCANCODE_BACKSPACE},
    {key_t::Delete, SDL_SCANCODE_DELETE},
    {key_t::Escape, SDL_SCANCODE_ESCAPE},

    // Modifiers
    {key_t::Left_Shift, SDL_SCANCODE_LSHIFT},
    {key_t::Right_Shift, SDL_SCANCODE_RSHIFT},
    {key_t::Left_Ctrl, SDL_SCANCODE_LCTRL},
    {key_t::Right_Ctrl, SDL_SCANCODE_RCTRL},
    {key_t::Left_Alt, SDL_SCANCODE_LALT},
    {key_t::Right_Alt, SDL_SCANCODE_RALT},
    {key_t::Left_Gui, SDL_SCANCODE_LGUI},
    {key_t::Right_Gui, SDL_SCANCODE_RGUI},

    // Arrows
    {key_t::Arrow_Left, SDL_SCANCODE_LEFT},
    {key_t::Arrow_Right, SDL_SCANCODE_RIGHT},
    {key_t::Arrow_Up, SDL_SCANCODE_UP},
    {key_t::Arrow_Down, SDL_SCANCODE_DOWN},

    // Punctuation
    {key_t::Left_Bracket, SDL_SCANCODE_LEFTBRACKET},
    {key_t::Right_Bracket, SDL_SCANCODE_RIGHTBRACKET},
    {key_t::Tilde, SDL_SCANCODE_GRAVE},

    // Keypad. SDL orders these KP_1..KP_9 then KP_0, so they are NOT contiguous
    // in scancode order -- hence the explicit rows rather than a loop.
    {key_t::Keypad_0, SDL_SCANCODE_KP_0}, {key_t::Keypad_1, SDL_SCANCODE_KP_1},
    {key_t::Keypad_2, SDL_SCANCODE_KP_2}, {key_t::Keypad_3, SDL_SCANCODE_KP_3},
    {key_t::Keypad_4, SDL_SCANCODE_KP_4}, {key_t::Keypad_5, SDL_SCANCODE_KP_5},
    {key_t::Keypad_6, SDL_SCANCODE_KP_6}, {key_t::Keypad_7, SDL_SCANCODE_KP_7},
    {key_t::Keypad_8, SDL_SCANCODE_KP_8}, {key_t::Keypad_9, SDL_SCANCODE_KP_9},
};

constexpr std::array<int, key_count> build_key_to_scancode()
{
  std::array<int, key_count> table{};
  for (auto &slot : table)
    slot = SDL_SCANCODE_UNKNOWN;
  for (const auto &mapping : key_mappings)
    table[static_cast<size_t>(mapping.key)] = mapping.scancode;
  return table;
}

constexpr std::array<key_t, SDL_NUM_SCANCODES> build_scancode_to_key()
{
  std::array<key_t, SDL_NUM_SCANCODES> table{};
  for (auto &slot : table)
    slot = key_t::Unknown;
  for (const auto &mapping : key_mappings)
    if (mapping.scancode > 0 && mapping.scancode < SDL_NUM_SCANCODES)
      table[mapping.scancode] = mapping.key;
  return table;
}

constexpr auto g_key_to_scancode = build_key_to_scancode();
constexpr auto g_scancode_to_key = build_scancode_to_key();

// --- key_t <-> Windows virtual-key table --------------------------------------
//
// The raw-input thread reports VK codes, not SDL scancodes, so this is the
// second half of the same job as the table above and is written the same way:
// one row list, one built lookup, no chance of the two directions drifting.
// The codes are numeric rather than VK_* names so the table survives a
// non-Windows build; they are a frozen part of the Windows ABI.
//
// Only the keys whose EDGE is worth a sub-tick slot need a row -- everything
// else reaches the game through SDL's levels and shortcut queue, which raw
// input does not replace. Adding a button to Button::Subtick_Tracked means
// adding its row here.

struct vk_mapping_t
{
  key_t    key;
  uint16_t virtual_key;
};

constexpr vk_mapping_t vk_mappings[] = {
    {key_t::W, 0x57},
    {key_t::A, 0x41},
    {key_t::S, 0x53},
    {key_t::D, 0x44},
    {key_t::R, 0x52},
    {key_t::Space, 0x20},
    {key_t::Num_0, 0x30},
    {key_t::Num_1, 0x31},
    {key_t::Num_2, 0x32},
    {key_t::Num_3, 0x33},
    {key_t::Num_4, 0x34},
    {key_t::Num_5, 0x35},
    {key_t::Num_6, 0x36},
    {key_t::Num_7, 0x37},
    {key_t::Num_8, 0x38},
    {key_t::Num_9, 0x39},
};

constexpr size_t virtual_key_count = 256;

constexpr std::array<key_t, virtual_key_count> build_virtual_key_to_key()
{
  std::array<key_t, virtual_key_count> table{};
  for (auto &slot : table)
    slot = key_t::Unknown;
  for (const auto &mapping : vk_mappings)
    table[mapping.virtual_key] = mapping.key;
  return table;
}

// Which keys raw input can ever report, so a resync after a focus change only
// pushes edges for keys that will get a matching release later.
constexpr std::array<bool, key_count> build_key_is_raw_reportable()
{
  std::array<bool, key_count> table{};
  for (const auto &mapping : vk_mappings)
    table[static_cast<size_t>(mapping.key)] = true;
  return table;
}

constexpr auto g_virtual_key_to_key    = build_virtual_key_to_key();
constexpr auto g_key_is_raw_reportable = build_key_is_raw_reportable();

key_t virtual_key_to_key(uint16_t virtual_key)
{
  if (virtual_key >= virtual_key_count)
    return key_t::Unknown;
  return g_virtual_key_to_key[virtual_key];
}

key_t scancode_to_key(int scancode)
{
  if (scancode <= 0 || scancode >= SDL_NUM_SCANCODES)
    return key_t::Unknown;
  return g_scancode_to_key[scancode];
}

mouse_button_t sdl_button_to_mouse_button(uint8_t sdl_button)
{
  switch (sdl_button)
  {
  case SDL_BUTTON_LEFT:   return mouse_button_t::Left;
  case SDL_BUTTON_MIDDLE: return mouse_button_t::Middle;
  case SDL_BUTTON_RIGHT:  return mouse_button_t::Right;
  case SDL_BUTTON_X1:     return mouse_button_t::X1;
  case SDL_BUTTON_X2:     return mouse_button_t::X2;
  default:                return mouse_button_t::Count;
  }
}

uint32_t mouse_button_to_sdl_mask(mouse_button_t button)
{
  switch (button)
  {
  case mouse_button_t::Left:   return SDL_BUTTON(SDL_BUTTON_LEFT);
  case mouse_button_t::Middle: return SDL_BUTTON(SDL_BUTTON_MIDDLE);
  case mouse_button_t::Right:  return SDL_BUTTON(SDL_BUTTON_RIGHT);
  case mouse_button_t::X1:     return SDL_BUTTON(SDL_BUTTON_X1);
  case mouse_button_t::X2:     return SDL_BUTTON(SDL_BUTTON_X2);
  default:                     return 0;
  }
}

modifiers_t modifiers_from_sdl_keymod(uint16_t sdl_mod)
{
  modifiers_t result{};
  result.shift = (sdl_mod & KMOD_SHIFT) != 0;
  result.ctrl = (sdl_mod & KMOD_CTRL) != 0;
  result.alt = (sdl_mod & KMOD_ALT) != 0;
  result.gui = (sdl_mod & KMOD_GUI) != 0;
  return result;
}

// --- Per-frame state ---------------------------------------------------------

std::array<bool, key_count> g_prev_key_down{};
std::array<bool, key_count> g_curr_key_down{};

std::array<bool, mouse_button_count> g_prev_mouse_down{};
std::array<bool, mouse_button_count> g_curr_mouse_down{};

int g_mouse_delta_x = 0;
int g_mouse_delta_y = 0;
float g_scroll_delta = 0.0f;

linalg::vec2i g_mouse_position{};
modifiers_t   g_modifiers{};

std::vector<key_event_t> g_key_events;
std::vector<mouse_button_event_t> g_mouse_button_events;
std::vector<input_edge_t> g_input_edges;

// --- Arrival clock -----------------------------------------------------------
//
// The domain input_edge_t is stamped in, and the one the raw-input thread uses.
// QueryPerformanceCounter on Windows so the two are literally the same counter;
// steady_clock elsewhere, where there is no raw-input thread and the stamps are
// frame-granular anyway.

uint64_t g_arrival_clock_frequency = 1'000'000'000ull;
bool     g_raw_input_active        = false;
bool     g_raw_input_focused       = false;

input_frame_span_t g_frame_span{};

uint64_t read_arrival_clock()
{
#ifdef _WIN32
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return static_cast<uint64_t>(now.QuadPart);
#else
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
#endif
}

// --- Raw-input drain ---------------------------------------------------------
//
// The level arrays above are the OTHER way of knowing which buttons are down,
// and these two are deliberately not derived from each other: the disagreement
// between them is what catches a transition the game never saw (see the
// lost-transition check in play_state.cpp).

std::array<bool, key_count>          g_raw_key_down{};
std::array<bool, mouse_button_count> g_raw_mouse_down{};

// --- Motion source ---
//
// A MODE, not a per-frame decision, and that is the whole reason it exists.
// Choosing per frame ("no raw motion arrived, so use SDL's delta") double-counts
// the moment the two drains straddle a frame boundary differently, which at low
// travel rates is most frames. So raw owns the aim outright once it is live.
//
// The one way it is not live while raw input is up is an absolute-coordinate
// device (a tablet, a touchscreen, an RDP session): those report no travel at
// all, so nothing would ever reach the aim. That is what the starvation counter
// below catches -- SDL saying the mouse moved while raw says it did not, for a
// second straight, means the deltas are not coming and we say so and fall back.
bool     g_motion_edges_are_live      = false;
uint32_t g_motion_starved_frame_count = 0;
bool     g_saw_raw_motion_this_frame  = false;

constexpr uint32_t MOTION_STARVED_FRAMES_BEFORE_FALLBACK = 60;

void push_motion_edge(int32_t delta_x, int32_t delta_y, uint64_t arrival_qpc_ticks)
{
  if (delta_x == 0 && delta_y == 0)
    return;

  input_edge_t edge{};
  edge.device            = input_device_t::Mouse_Motion;
  edge.motion            = {delta_x, delta_y};
  edge.arrival_qpc_ticks = arrival_qpc_ticks;
  g_input_edges.push_back(edge);
}

// Auto-repeat is a make code with no break behind it, so raw input reports a
// held key over and over. An edge is a CHANGE; this is where that is enforced,
// which is the job SDL's event.key.repeat flag does on the fallback path.
void push_raw_key_edge(key_t key, bool down, uint64_t arrival_qpc_ticks)
{
  const size_t index = static_cast<size_t>(key);
  if (index >= key_count || g_raw_key_down[index] == down)
    return;
  g_raw_key_down[index] = down;

  input_edge_t edge{};
  edge.device            = input_device_t::Key;
  edge.key               = key;
  edge.down              = down;
  edge.arrival_qpc_ticks = arrival_qpc_ticks;
  g_input_edges.push_back(edge);
}

void push_raw_mouse_edge(mouse_button_t button, bool down, uint64_t arrival_qpc_ticks)
{
  const size_t index = static_cast<size_t>(button);
  if (index >= mouse_button_count || g_raw_mouse_down[index] == down)
    return;
  g_raw_mouse_down[index] = down;

  input_edge_t edge{};
  edge.device            = input_device_t::Mouse_Button;
  edge.button            = button;
  edge.down              = down;
  edge.arrival_qpc_ticks = arrival_qpc_ticks;
  g_input_edges.push_back(edge);
}

void discard_raw_input()
{
  raw_input::raw_input_event_t events[512];
  while (raw_input::drain(events) == std::size(events))
    ;
}

// Everything held goes up at the frame boundary. Losing focus with a key down
// would otherwise stick it down forever, since the edges that release it are
// delivered to whoever took the keyboard.
void release_everything_held()
{
  for (size_t index = 0; index < key_count; ++index)
    push_raw_key_edge(static_cast<key_t>(index), false, g_frame_span.end_qpc_ticks);
  for (size_t index = 0; index < mouse_button_count; ++index)
    push_raw_mouse_edge(static_cast<mouse_button_t>(index), false, g_frame_span.end_qpc_ticks);
}

// Coming back with a key already held is a real transition from here: the press
// itself went to another application. Levels are the only source that knows,
// and new_frame has just refreshed them.
void resync_held_from_levels()
{
  for (size_t index = 0; index < key_count; ++index)
    if (g_key_is_raw_reportable[index])
      push_raw_key_edge(static_cast<key_t>(index), g_curr_key_down[index],
                        g_frame_span.end_qpc_ticks);
  for (size_t index = 0; index < mouse_button_count; ++index)
    push_raw_mouse_edge(static_cast<mouse_button_t>(index), g_curr_mouse_down[index],
                        g_frame_span.end_qpc_ticks);
}

void drain_raw_input()
{
  // RIDEV_INPUTSINK keeps the thread stamping while another application has the
  // keyboard, and the gate is HERE rather than in the thread on purpose: the
  // input thread must not learn about game state, and this side already knows
  // whether the window has focus.
  const bool window_focused = SDL_GetKeyboardFocus() != nullptr;

  if (!window_focused)
  {
    discard_raw_input();
    if (g_raw_input_focused)
    {
      release_everything_held();
      g_raw_input_focused = false;
    }
    return;
  }

  if (!g_raw_input_focused)
  {
    discard_raw_input();
    g_raw_input_focused = true;
    resync_held_from_levels();
    return;
  }

  raw_input::raw_input_event_t events[512];
  for (;;)
  {
    const size_t count = raw_input::drain(events);
    for (size_t index = 0; index < count; ++index)
    {
      const raw_input::raw_input_event_t &event = events[index];

      // The span end was read just BEFORE this drain, so an input landing in
      // the microseconds between the two reads is real and later than it.
      // Stretch the span to hold it rather than reporting it as out of range:
      // the next frame starts where this one ends, so nothing is double-counted
      // and no arrival can fall in a gap.
      g_frame_span.end_qpc_ticks =
          std::max(g_frame_span.end_qpc_ticks, event.arrival_qpc_ticks);

      switch (event.kind)
      {
      case raw_input::raw_input_kind_t::Key:
      {
        const key_t key = virtual_key_to_key(event.code);
        if (key != key_t::Unknown)
          push_raw_key_edge(key, event.down, event.arrival_qpc_ticks);
        break;
      }
      case raw_input::raw_input_kind_t::Mouse_Button:
        if (event.code < mouse_button_count)
          push_raw_mouse_edge(static_cast<mouse_button_t>(event.code), event.down,
                              event.arrival_qpc_ticks);
        break;
      case raw_input::raw_input_kind_t::Mouse_Motion:
        // SDL still owns the POINTER -- where the cursor is, what a menu clicks
        // on. This is travel, which is what the aim integrates, and it is in
        // the same ordered list as the transitions so a shot can be resolved
        // against the motion that preceded it rather than the frame's total.
        g_saw_raw_motion_this_frame = true;
        push_motion_edge(event.delta_x, event.delta_y, event.arrival_qpc_ticks);
        break;
      }
    }
    if (count < std::size(events))
      break;
  }
}

} // namespace

void init()
{
  const std::optional<uint64_t> frequency = raw_input::try_start();
  if (frequency.has_value() && *frequency != 0)
  {
    g_arrival_clock_frequency = *frequency;
    g_raw_input_active        = true;
    g_motion_edges_are_live   = true;
  }
  else
  {
    g_raw_input_active = false;
#ifdef _WIN32
    LARGE_INTEGER counter_frequency{};
    QueryPerformanceFrequency(&counter_frequency);
    g_arrival_clock_frequency = static_cast<uint64_t>(counter_frequency.QuadPart);
#endif
    log_warning("input: the raw-input thread did not start; falling back to SDL "
                "transitions stamped at the frame boundary. Sub-tick edge times "
                "are frame-granular until it does");
  }

  g_frame_span.start_qpc_ticks = read_arrival_clock();
  g_frame_span.end_qpc_ticks   = g_frame_span.start_qpc_ticks;
}

void shutdown()
{
  raw_input::stop();
  g_raw_input_active = false;
}

void new_frame()
{
  SDL_GetRelativeMouseState(&g_mouse_delta_x, &g_mouse_delta_y);
  g_scroll_delta = 0.0f;

  // Walk SDL's keyboard state once and project into our key_t-indexed arrays.
  const Uint8 *sdl_state = SDL_GetKeyboardState(nullptr);
  g_prev_key_down = g_curr_key_down;
  for (size_t i = 0; i < key_count; ++i)
  {
    int scancode = g_key_to_scancode[i];
    g_curr_key_down[i] = (scancode > 0) && (sdl_state[scancode] != 0);
  }

  // Same treatment for the mouse, read at the same instant as the keyboard
  // above so the two can be folded into one bitfield and compared against the
  // edges. Position and modifiers ride along for the same reason: one instant
  // for every level this layer reports.
  int mouse_x = 0;
  int mouse_y = 0;
  uint32_t mouse_state = SDL_GetMouseState(&mouse_x, &mouse_y);
  g_mouse_position = {mouse_x, mouse_y};
  g_modifiers = modifiers_from_sdl_keymod(static_cast<uint16_t>(SDL_GetModState()));
  g_prev_mouse_down = g_curr_mouse_down;
  for (size_t i = 0; i < mouse_button_count; ++i)
  {
    uint32_t mask = mouse_button_to_sdl_mask(static_cast<mouse_button_t>(i));
    g_curr_mouse_down[i] = (mask != 0) && ((mouse_state & mask) != 0);
  }

  g_key_events.clear();
  g_mouse_button_events.clear();
  g_input_edges.clear();

  // The span endpoints are both read here, so consecutive frames tile the
  // timeline with no gap and every edge drained below falls inside this one.
  g_frame_span.start_qpc_ticks = g_frame_span.end_qpc_ticks;
  g_frame_span.end_qpc_ticks   = read_arrival_clock();

  // After the level snapshot above, which resync_held_from_levels reads.
  g_saw_raw_motion_this_frame = false;
  if (g_raw_input_active)
    drain_raw_input();

  const bool sdl_reports_motion = (g_mouse_delta_x != 0 || g_mouse_delta_y != 0);

  if (g_motion_edges_are_live)
  {
    // Only SDL seeing travel means the raw deltas are not coming -- an
    // absolute-coordinate device, whose reports were dropped at the source
    // because a screen coordinate is not a delta. Loud and one-way: silently
    // limping along on a frame-granular aim is exactly what this whole path
    // exists to stop doing.
    g_motion_starved_frame_count =
        (sdl_reports_motion && !g_saw_raw_motion_this_frame) ? g_motion_starved_frame_count + 1 : 0;

    if (g_motion_starved_frame_count >= MOTION_STARVED_FRAMES_BEFORE_FALLBACK)
    {
      log_error("input: raw input reported no mouse travel for {} frames while SDL did. "
                "The device is reporting absolute coordinates rather than deltas; falling "
                "back to SDL motion, which makes the aim frame-granular",
                g_motion_starved_frame_count);
      g_motion_edges_are_live = false;
    }
  }

  // One synthetic edge carrying the whole frame's travel, stamped at the span
  // end exactly like the fallback transitions are. Sub-tick aim then degrades
  // to what it was before raw input: every step of the tick runs under the
  // frame's finished angle, with no second code path anywhere above this.
  if (!g_motion_edges_are_live)
    push_motion_edge(g_mouse_delta_x, g_mouse_delta_y, g_frame_span.end_qpc_ticks);
}

void process_sdl_event(const void *sdl_event)
{
  const SDL_Event *event = static_cast<const SDL_Event *>(sdl_event);
  switch (event->type)
  {
  case SDL_KEYDOWN:
  case SDL_KEYUP:
  {
    key_t key = scancode_to_key(event->key.keysym.scancode);
    if (key == key_t::Unknown)
      return;

    // A key REPEAT is the OS typing for you, not a transition, so it reaches the
    // shortcut queue (where holding backspace should keep deleting) and never
    // the edge queue (where it would be a press with no release behind it).
    // The FALLBACK edge source, live only when the raw-input thread is not.
    // SDL stamps at pump time, so there is no moment inside the frame to
    // recover -- the frame boundary is the honest place to put it, and the
    // sub-tick fold then behaves exactly as it did before raw input existed.
    if (!g_raw_input_active && event->key.repeat == 0)
    {
      input_edge_t edge{};
      edge.device            = input_device_t::Key;
      edge.key               = key;
      edge.down              = event->type == SDL_KEYDOWN;
      edge.arrival_qpc_ticks = g_frame_span.end_qpc_ticks;
      g_input_edges.push_back(edge);
    }

    if (event->type != SDL_KEYDOWN)
      break;

    key_event_t key_event{};
    key_event.key = key;
    key_event.mods = modifiers_from_sdl_keymod(event->key.keysym.mod);
    key_event.repeat = event->key.repeat != 0;
    g_key_events.push_back(key_event);
    break;
  }
  case SDL_MOUSEBUTTONDOWN:
  case SDL_MOUSEBUTTONUP:
  {
    mouse_button_t button = sdl_button_to_mouse_button(event->button.button);
    if (button == mouse_button_t::Count)
      return;
    mouse_button_event_t button_event{};
    button_event.button = button;
    button_event.action = (event->type == SDL_MOUSEBUTTONDOWN)
                              ? mouse_action_t::Down
                              : mouse_action_t::Up;
    button_event.position = {event->button.x, event->button.y};
    button_event.mods = modifiers_from_sdl_keymod(SDL_GetModState());
    g_mouse_button_events.push_back(button_event);

    if (!g_raw_input_active)
    {
      input_edge_t edge{};
      edge.device            = input_device_t::Mouse_Button;
      edge.button            = button;
      edge.down              = event->type == SDL_MOUSEBUTTONDOWN;
      edge.arrival_qpc_ticks = g_frame_span.end_qpc_ticks;
      g_input_edges.push_back(edge);
    }
    break;
  }
  case SDL_MOUSEWHEEL:
    g_scroll_delta += static_cast<float>(event->wheel.y);
    break;
  default:
    break;
  }
}

bool is_key_down(key_t key)
{
  size_t index = static_cast<size_t>(key);
  if (index >= key_count)
    return false;
  return g_curr_key_down[index];
}

bool is_key_pressed(key_t key)
{
  size_t index = static_cast<size_t>(key);
  if (index >= key_count)
    return false;
  return g_curr_key_down[index] && !g_prev_key_down[index];
}

bool is_mouse_down(mouse_button_t button)
{
  size_t index = static_cast<size_t>(button);
  if (index >= mouse_button_count)
    return false;
  return g_curr_mouse_down[index];
}

bool is_mouse_pressed(mouse_button_t button)
{
  size_t index = static_cast<size_t>(button);
  if (index >= mouse_button_count)
    return false;
  return g_curr_mouse_down[index] && !g_prev_mouse_down[index];
}

modifiers_t current_modifiers()
{
  return g_modifiers;
}

linalg::vec2i mouse_position()
{
  return g_mouse_position;
}

linalg::vec2i mouse_delta()
{
  return {g_mouse_delta_x, g_mouse_delta_y};
}

float scroll_delta()
{
  return g_scroll_delta;
}

void set_relative_mouse_mode(bool enabled)
{
  SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

Span<const key_event_t> frame_key_events()
{
  return g_key_events;
}

Span<const mouse_button_event_t> frame_mouse_button_events()
{
  return g_mouse_button_events;
}

Span<const input_edge_t> frame_input_edges()
{
  return g_input_edges;
}

input_frame_span_t frame_arrival_span()
{
  return g_frame_span;
}

uint64_t arrival_clock_now()
{
  return read_arrival_clock();
}

uint64_t arrival_clock_frequency()
{
  return g_arrival_clock_frequency;
}

bool raw_input_is_active()
{
  return g_raw_input_active;
}

bool imgui_wants_mouse()
{
  return ImGui::GetIO().WantCaptureMouse;
}

bool imgui_wants_keyboard()
{
  return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace client::input
